// Development worker: PostgreSQL leases and bounded immutable-object validation.
package main

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"io"
	"log"
	"os"
	"time"

	"github.com/jackc/pgx/v5/pgxpool"
	"github.com/klauspost/compress/zstd"
	"github.com/minio/minio-go/v7"
	"github.com/minio/minio-go/v7/pkg/credentials"
)

const maxObjectBytes int64 = 64 << 20
const maxDecodedObjectBytes int64 = 256 << 20

// validCompressedContainer first bounds decompression. SessionObject is protobuf,
// whose first field is the required header (field 1, length-delimited); checking
// that envelope before any later semantic projection keeps arbitrary blobs out of
// the accepted path without treating the uploaded data as executable content.
func validCompressedContainer(compressed []byte) bool {
	decoder, err := zstd.NewReader(bytes.NewReader(compressed))
	if err != nil {
		return false
	}
	decompressed, err := io.ReadAll(io.LimitReader(decoder, maxDecodedObjectBytes+1))
	decoder.Close()
	if err != nil || len(decompressed) == 0 || int64(len(decompressed)) > maxDecodedObjectBytes {
		return false
	}
	// Protobuf field 1 with wire type 2 is encoded as 0x0a. The full field-level
	// validation belongs with the future trusted projection, but an empty or
	// wrong-envelope object cannot be accepted by this development worker.
	return decompressed[0] == 0x0a
}

func main() {
	ctx := context.Background()
	db, err := pgxpool.New(ctx, os.Getenv("VIR_POSTGRES_DSN"))
	if err != nil {
		log.Fatal(err)
	}
	defer db.Close()
	object, err := minio.New(os.Getenv("VIR_MINIO_INTERNAL_ENDPOINT"), &minio.Options{Creds: credentials.NewStaticV4(os.Getenv("VIR_MINIO_ACCESS_KEY"), os.Getenv("VIR_MINIO_SECRET_KEY"), ""), Secure: false})
	if err != nil {
		log.Fatal(err)
	}
	bucket := os.Getenv("VIR_MINIO_BUCKET")
	if bucket == "" {
		bucket = "development-session-objects"
	}
	for {
		process(ctx, db, object, bucket)
		time.Sleep(500 * time.Millisecond)
	}
}

func process(ctx context.Context, db *pgxpool.Pool, object *minio.Client, bucket string) {
	tx, err := db.Begin(ctx)
	if err != nil {
		return
	}
	defer tx.Rollback(ctx)
	var identity, session, digest, key string
	err = tx.QueryRow(ctx, `WITH next AS (
  SELECT identity_id, session_id FROM sessions
  WHERE status = 'processing' AND (processing_lease_until IS NULL OR processing_lease_until < now())
  ORDER BY created_at LIMIT 1 FOR UPDATE SKIP LOCKED
)
UPDATE sessions AS s SET processing_lease_owner = 'development-worker',
  processing_lease_until = now() + interval '30 seconds'
FROM next WHERE s.identity_id = next.identity_id AND s.session_id = next.session_id
RETURNING s.identity_id, s.session_id::text, s.object_sha256`).Scan(&identity, &session, &digest)
	if err != nil {
		return
	}
	if err = tx.QueryRow(ctx, "SELECT object_key FROM session_objects WHERE identity_id=$1 AND session_id=$2", identity, session).Scan(&key); err != nil {
		return
	}
	if err = tx.Commit(ctx); err != nil {
		return
	}

	status := "accepted_with_warnings"
	reader, err := object.GetObject(ctx, bucket, key, minio.GetObjectOptions{})
	if err != nil {
		status = "rejected"
	} else {
		defer reader.Close()
		body, copyErr := io.ReadAll(io.LimitReader(reader, maxObjectBytes+1))
		sum := sha256.Sum256(body)
		if copyErr != nil || len(body) == 0 || int64(len(body)) > maxObjectBytes || hex.EncodeToString(sum[:]) != digest || !validCompressedContainer(body) {
			status = "rejected"
		}
	}
	_, _ = db.Exec(ctx, "UPDATE sessions SET status=$3,processing_lease_owner=NULL,processing_lease_until=NULL,revision=revision+1 WHERE identity_id=$1 AND session_id=$2 AND processing_lease_owner='development-worker'", identity, session, status)
	_, _ = db.Exec(ctx, "UPDATE session_objects SET status=$3 WHERE identity_id=$1 AND session_id=$2", identity, session, status)
}
