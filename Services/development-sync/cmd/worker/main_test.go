package main

import (
	"testing"

	"github.com/klauspost/compress/zstd"
)

func TestWorkerObjectLimitIsBounded(t *testing.T) {
	if maxObjectBytes != 64<<20 {
		t.Fatalf("object limit = %d", maxObjectBytes)
	}
}

func TestWorkerRejectsInvalidCompressedContainer(t *testing.T) {
	if validCompressedContainer([]byte("not zstd")) {
		t.Fatal("invalid compressed bytes were accepted")
	}
	encoder, err := zstd.NewWriter(nil)
	if err != nil {
		t.Fatal(err)
	}
	defer encoder.Close()
	if validCompressedContainer(encoder.EncodeAll([]byte{0x08, 0x01}, nil)) {
		t.Fatal("wrong protobuf envelope was accepted")
	}
	if !validCompressedContainer(encoder.EncodeAll([]byte{0x0a, 0x00}, nil)) {
		t.Fatal("bounded SessionObject envelope was rejected")
	}
}
