package main

import "testing"

func TestContractValidatorsRejectNonCanonicalValues(t *testing.T) {
	if !sessionIDOK("018b0c15-1234-7abc-8def-0123456789ab") || sessionIDOK("018B0C15-1234-7abc-8def-0123456789ab") {
		t.Fatal("UUID validation accepts an invalid contract value")
	}
	if !digestOK("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") || digestOK("not-a-digest") {
		t.Fatal("digest validation accepts an invalid contract value")
	}
	if !dispositionOK("completed") || dispositionOK("invented") {
		t.Fatal("disposition validation accepts an invalid contract value")
	}
}

func TestObjectKeysRemainIdentityConfined(t *testing.T) {
	if got := objectKey("dev_a", "018b0c15-1234-7abc-8def-0123456789ab"); got != "development/dev_a/018b0c15-1234-7abc-8def-0123456789ab.zst" {
		t.Fatalf("object key = %q", got)
	}
}
