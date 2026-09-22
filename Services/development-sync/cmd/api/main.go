// Development-only loopback API. PostgreSQL owns lifecycle state; MinIO owns bytes.
package main

import (
	"context"
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/hex"
	"encoding/json"
	"errors"
	"io"
	"log"
	"net/http"
	"os"
	"strings"
	"time"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgxpool"
	"github.com/minio/minio-go/v7"
	"github.com/minio/minio-go/v7/pkg/credentials"
)

type bootstrapRequest struct {
	RequestVersion  int    `json:"request_version"`
	BootstrapSecret string `json:"bootstrap_secret"`
}
type sessionRequest struct {
	RequestVersion int    `json:"request_version"`
	Disposition    string `json:"disposition"`
	ObjectSHA256   string `json:"object_sha256"`
}
type uploadCompleteRequest struct {
	RequestVersion   int    `json:"request_version"`
	SizeBytes        int64  `json:"size_bytes,omitempty"`
	ObjectSHA256     string `json:"object_sha256,omitempty"`
	Codec            string `json:"codec,omitempty"`
	ContainerVersion int    `json:"container_version,omitempty"`
	SchemaVersion    int    `json:"schema_version,omitempty"`
}
type problem struct {
	Type   string `json:"type"`
	Title  string `json:"title"`
	Status int    `json:"status"`
}
type server struct {
	secret           string
	db               *pgxpool.Pool
	internal, public *minio.Client
	bucket           string
}

var errIdempotencyConflict = errors.New("idempotency key conflict")

func randomToken() string {
	b := make([]byte, 32)
	if _, err := rand.Read(b); err != nil {
		panic(err)
	}
	return hex.EncodeToString(b)
}
func hash(v string) string { sum := sha256.Sum256([]byte(v)); return hex.EncodeToString(sum[:]) }
func writeProblem(w http.ResponseWriter, code int, title string) {
	w.Header().Set("Content-Type", "application/problem+json")
	w.WriteHeader(code)
	_ = json.NewEncoder(w).Encode(problem{"https://virtualrowing.dev/problems/development-sync", title, code})
}
func digestOK(v string) bool {
	if len(v) != 64 {
		return false
	}
	for _, c := range v {
		if !(c >= '0' && c <= '9' || c >= 'a' && c <= 'f') {
			return false
		}
	}
	return true
}
func dispositionOK(v string) bool { return v == "completed" || v == "interrupted" || v == "aborted" }
func sessionIDOK(v string) bool {
	if len(v) != 36 {
		return false
	}
	for i, c := range v {
		if i == 8 || i == 13 || i == 18 || i == 23 {
			if c != '-' {
				return false
			}
			continue
		}
		if !(c >= '0' && c <= '9' || c >= 'a' && c <= 'f') {
			return false
		}
	}
	return true
}
func objectKey(identity, sid string) string { return "development/" + identity + "/" + sid + ".zst" }

func parseSessionRoute(path string) (string, string, bool) {
	parts := strings.Split(strings.TrimPrefix(path, "/"), "/")
	if (len(parts) != 3 && len(parts) != 4) || parts[0] != "v1" || parts[1] != "sessions" || parts[2] == "" {
		return "", "", false
	}
	if len(parts) == 4 {
		if parts[3] == "" {
			return "", "", false
		}
		return parts[2], parts[3], true
	}
	return parts[2], "", true
}

func (s *server) auth(r *http.Request) (string, bool) {
	const p = "Bearer "
	h := r.Header.Get("Authorization")
	if !strings.HasPrefix(h, p) {
		return "", false
	}
	var id string
	err := s.db.QueryRow(r.Context(), "SELECT identity_id FROM development_tokens WHERE token_sha256=$1 AND expires_at > now()", hash(strings.TrimPrefix(h, p))).Scan(&id)
	return id, err == nil
}
func decodeSession(w http.ResponseWriter, r *http.Request) (sessionRequest, bool) {
	var v sessionRequest
	if json.NewDecoder(http.MaxBytesReader(w, r.Body, 16<<10)).Decode(&v) != nil || v.RequestVersion != 1 || !digestOK(v.ObjectSHA256) {
		writeProblem(w, 400, "invalid versioned session request")
		return v, false
	}
	return v, true
}
func requireKey(w http.ResponseWriter, r *http.Request) (string, bool) {
	k := r.Header.Get("Idempotency-Key")
	if k == "" || len(k) > 128 {
		writeProblem(w, 400, "Idempotency-Key is required")
		return "", false
	}
	return k, true
}
func fingerprint(parts ...string) string { return hash(strings.Join(parts, "|")) }

func (s *server) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	if r.Method == http.MethodPost && r.URL.Path == "/v1/development/bootstrap" {
		s.bootstrap(w, r)
		return
	}
	id, ok := s.auth(r)
	if !ok {
		writeProblem(w, 401, "valid development bearer token required")
		return
	}
	sid, action, ok := parseSessionRoute(r.URL.Path)
	if !ok {
		writeProblem(w, 404, "route not found")
		return
	}
	if !sessionIDOK(sid) {
		writeProblem(w, 400, "session_id must be a canonical lowercase UUID")
		return
	}
	switch {
	case r.Method == http.MethodPut && action == "":
		s.create(w, r, id, sid)
	case r.Method == http.MethodPost && action == "finalize":
		s.finalize(w, r, id, sid)
	case r.Method == http.MethodPost && action == "upload":
		s.upload(w, r, id, sid)
	case r.Method == http.MethodPost && action == "upload-complete":
		s.complete(w, r, id, sid)
	case r.Method == http.MethodGet && action == "processing-status":
		s.status(w, r, id, sid)
	default:
		writeProblem(w, 404, "route not found")
	}
}
func (s *server) bootstrap(w http.ResponseWriter, r *http.Request) {
	var v bootstrapRequest
	if json.NewDecoder(http.MaxBytesReader(w, r.Body, 4<<10)).Decode(&v) != nil || v.RequestVersion != 1 || subtle.ConstantTimeCompare([]byte(v.BootstrapSecret), []byte(s.secret)) != 1 {
		writeProblem(w, 401, "bootstrap authorization failed")
		return
	}
	id, token, expiry := "dev_"+randomToken(), randomToken(), time.Now().UTC().Add(24*time.Hour)
	tx, err := s.db.Begin(r.Context())
	if err == nil {
		_, err = tx.Exec(r.Context(), "INSERT INTO development_identities(identity_id) VALUES ($1)", id)
	}
	if err == nil {
		_, err = tx.Exec(r.Context(), "INSERT INTO development_tokens(token_sha256,identity_id,expires_at) VALUES ($1,$2,$3)", hash(token), id, expiry)
	}
	if err == nil {
		err = tx.Commit(r.Context())
	}
	if err != nil {
		writeProblem(w, 503, "development persistence unavailable")
		return
	}
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{"response_version": 1, "development_identity": id, "bearer_token": token, "expires_at": expiry.Format(time.RFC3339)})
}
func (s *server) idempotent(tx pgx.Tx, ctx context.Context, id, key, request string) (bool, error) {
	var old string
	err := tx.QueryRow(ctx, "SELECT request_sha256 FROM idempotency_keys WHERE identity_id=$1 AND idempotency_key=$2", id, key).Scan(&old)
	if err == nil {
		if old != request {
			return false, errIdempotencyConflict
		}
		return true, nil
	}
	if err != pgx.ErrNoRows {
		return false, err
	}
	_, err = tx.Exec(ctx, "INSERT INTO idempotency_keys(identity_id,idempotency_key,request_sha256,response_json) VALUES ($1,$2,$3,'{}'::jsonb)", id, key, request)
	return false, err
}
func (s *server) create(w http.ResponseWriter, r *http.Request, id, sid string) {
	v, ok := decodeSession(w, r)
	if !ok {
		return
	}
	if !dispositionOK(v.Disposition) {
		writeProblem(w, 400, "invalid session disposition")
		return
	}
	key, ok := requireKey(w, r)
	if !ok {
		return
	}
	tx, err := s.db.Begin(r.Context())
	if err != nil {
		writeProblem(w, 503, "development persistence unavailable")
		return
	}
	defer tx.Rollback(r.Context())
	replay, err := s.idempotent(tx, r.Context(), id, "create:"+key, fingerprint("create", sid, v.ObjectSHA256, v.Disposition))
	if err == nil && !replay {
		_, err = tx.Exec(r.Context(), "INSERT INTO sessions(session_id,identity_id,disposition,object_sha256,status) VALUES ($1,$2,$3,$4,'queued') ON CONFLICT (identity_id,session_id) DO NOTHING", sid, id, v.Disposition, v.ObjectSHA256)
	}
	var digest, disposition string
	if err == nil {
		err = tx.QueryRow(r.Context(), "SELECT object_sha256,disposition FROM sessions WHERE identity_id=$1 AND session_id=$2", id, sid).Scan(&digest, &disposition)
		if digest != v.ObjectSHA256 || disposition != v.Disposition {
			err = pgx.ErrTxClosed
		}
	}
	if err == nil {
		err = tx.Commit(r.Context())
	}
	if err != nil {
		writeProblem(w, 409, "session or idempotency conflict")
		return
	}
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{"response_version": 1, "session_id": sid})
}
func (s *server) finalize(w http.ResponseWriter, r *http.Request, id, sid string) {
	v, ok := decodeSession(w, r)
	if !ok {
		return
	}
	key, ok := requireKey(w, r)
	if !ok {
		return
	}
	tx, err := s.db.Begin(r.Context())
	if err != nil {
		writeProblem(w, 503, "development persistence unavailable")
		return
	}
	defer tx.Rollback(r.Context())
	replay, err := s.idempotent(tx, r.Context(), id, "finalize:"+key, fingerprint("finalize", sid, v.ObjectSHA256))
	if err == nil && !replay {
		var revision int64
		err = tx.QueryRow(r.Context(), "UPDATE sessions SET status='queued',finalized_at=COALESCE(finalized_at,now()),revision=revision+1 WHERE identity_id=$1 AND session_id=$2 AND object_sha256=$3 RETURNING revision", id, sid, v.ObjectSHA256).Scan(&revision)
	}
	if err == nil {
		err = tx.Commit(r.Context())
	}
	if err != nil {
		writeProblem(w, 409, "session, digest, or idempotency conflict")
		return
	}
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{"response_version": 1, "status": "queued"})
}
func (s *server) upload(w http.ResponseWriter, r *http.Request, id, sid string) {
	var digest string
	err := s.db.QueryRow(r.Context(), "SELECT object_sha256 FROM sessions WHERE identity_id=$1 AND session_id=$2 AND finalized_at IS NOT NULL AND status IN ('queued','uploading','processing')", id, sid).Scan(&digest)
	if err != nil {
		writeProblem(w, 404, "finalized session not found")
		return
	}
	key := objectKey(id, sid)
	url, err := s.public.PresignedPutObject(r.Context(), s.bucket, key, 5*time.Minute)
	if err == nil {
		_, err = s.db.Exec(r.Context(), "UPDATE sessions SET status='uploading' WHERE identity_id=$1 AND session_id=$2", id, sid)
	}
	if err != nil {
		writeProblem(w, 503, "object store unavailable")
		return
	}
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{"response_version": 1, "object_key": key, "upload_url": url.String(), "expected_sha256": digest})
}
func (s *server) complete(w http.ResponseWriter, r *http.Request, id, sid string) {
	var v uploadCompleteRequest
	if err := json.NewDecoder(http.MaxBytesReader(w, r.Body, 4<<10)).Decode(&v); err != nil && err != io.EOF {
		writeProblem(w, 400, "invalid upload completion request")
		return
	}
	if v.RequestVersion != 0 && v.RequestVersion != 1 {
		writeProblem(w, 400, "invalid upload completion version")
		return
	}
	var digest string
	err := s.db.QueryRow(r.Context(), "SELECT object_sha256 FROM sessions WHERE identity_id=$1 AND session_id=$2 AND finalized_at IS NOT NULL AND status IN ('uploading','processing')", id, sid).Scan(&digest)
	if err != nil {
		writeProblem(w, 404, "uploading session not found")
		return
	}
	if v.ObjectSHA256 != "" && v.ObjectSHA256 != digest {
		writeProblem(w, 409, "object digest conflict")
		return
	}
	if v.SizeBytes < 0 || (v.SizeBytes > 0 && v.SizeBytes > 64<<20) || (v.Codec != "" && v.Codec != "zstd") || (v.ContainerVersion != 0 && v.ContainerVersion != 1) || (v.SchemaVersion != 0 && v.SchemaVersion != 2) {
		writeProblem(w, 400, "invalid upload completion metadata")
		return
	}
	info, err := s.internal.StatObject(r.Context(), s.bucket, objectKey(id, sid), minio.StatObjectOptions{})
	if err != nil || info.Size < 1 || info.Size > 64<<20 {
		writeProblem(w, 422, "uploaded object unavailable or exceeds development limit")
		return
	}
	if v.SizeBytes > 0 && v.SizeBytes != info.Size {
		writeProblem(w, 409, "object size conflict")
		return
	}
	_, err = s.db.Exec(r.Context(), "INSERT INTO session_objects(identity_id,session_id,object_sha256,object_key,size_bytes,status) VALUES ($1,$2,$3,$4,$5,'processing') ON CONFLICT (identity_id,session_id) DO UPDATE SET size_bytes=EXCLUDED.size_bytes,status='processing'", id, sid, digest, objectKey(id, sid), info.Size)
	if err == nil {
		_, err = s.db.Exec(r.Context(), "UPDATE sessions SET status='processing',revision=revision+1 WHERE identity_id=$1 AND session_id=$2", id, sid)
	}
	if err != nil {
		writeProblem(w, 503, "development persistence unavailable")
		return
	}
	w.WriteHeader(http.StatusAccepted)
}
func (s *server) status(w http.ResponseWriter, r *http.Request, id, sid string) {
	var status string
	var revision int64
	err := s.db.QueryRow(r.Context(), "SELECT status,revision FROM sessions WHERE identity_id=$1 AND session_id=$2", id, sid).Scan(&status, &revision)
	if err != nil {
		writeProblem(w, 404, "session not found")
		return
	}
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{"response_version": 1, "status": status, "revision": revision})
}
func makeClient(endpoint, access, secret string) (*minio.Client, error) {
	return minio.New(endpoint, &minio.Options{Creds: credentials.NewStaticV4(access, secret, ""), Secure: false})
}
func main() {
	secret, dsn := os.Getenv("VIR_BOOTSTRAP_SECRET"), os.Getenv("VIR_POSTGRES_DSN")
	if len(secret) < 32 || dsn == "" {
		log.Fatal("VIR_BOOTSTRAP_SECRET and VIR_POSTGRES_DSN are required")
	}
	ctx := context.Background()
	db, err := pgxpool.New(ctx, dsn)
	if err != nil {
		log.Fatal(err)
	}
	defer db.Close()
	if err = db.Ping(ctx); err != nil {
		log.Fatal(err)
	}
	access, key := os.Getenv("VIR_MINIO_ACCESS_KEY"), os.Getenv("VIR_MINIO_SECRET_KEY")
	internal, err := makeClient(os.Getenv("VIR_MINIO_INTERNAL_ENDPOINT"), access, key)
	if err != nil {
		log.Fatal(err)
	}
	public, err := makeClient(os.Getenv("VIR_MINIO_PUBLIC_ENDPOINT"), access, key)
	if err != nil {
		log.Fatal(err)
	}
	bucket := os.Getenv("VIR_MINIO_BUCKET")
	if bucket == "" {
		bucket = "development-session-objects"
	}
	exists, err := internal.BucketExists(ctx, bucket)
	if err != nil {
		log.Fatal(err)
	}
	if !exists {
		if err = internal.MakeBucket(ctx, bucket, minio.MakeBucketOptions{}); err != nil {
			log.Fatal(err)
		}
	}
	httpServer := &http.Server{Addr: ":8080", Handler: &server{secret: secret, db: db, internal: internal, public: public, bucket: bucket}, ReadHeaderTimeout: 5 * time.Second, ReadTimeout: 15 * time.Second, WriteTimeout: 15 * time.Second, IdleTimeout: 30 * time.Second}
	log.Fatal(httpServer.ListenAndServe())
}
