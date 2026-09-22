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

func TestSessionRoutesMatchOnlyDeclaredOpenAPIPaths(t *testing.T) {
	const sessionID = "018b0c15-1234-7abc-8def-0123456789ab"
	tests := []struct {
		path   string
		sid    string
		action string
		ok     bool
	}{
		{"/v1/sessions/" + sessionID, sessionID, "", true},
		{"/v1/sessions/" + sessionID + "/finalize", sessionID, "finalize", true},
		{"/v1/sessions/" + sessionID + "/upload", sessionID, "upload", true},
		{"/v1/sessions/" + sessionID + "/upload-complete", sessionID, "upload-complete", true},
		{"/v1/sessions/" + sessionID + "/processing-status", sessionID, "processing-status", true},
		{"/v1/sessions/" + sessionID + "/", "", "", false},
		{"/v1/sessions/" + sessionID + "/finalize/extra", "", "", false},
		{"/v1/sessions/" + sessionID + "/extra/path", "", "", false},
		{"//v1/sessions/" + sessionID, "", "", false},
	}
	for _, test := range tests {
		sid, action, ok := parseSessionRoute(test.path)
		if sid != test.sid || action != test.action || ok != test.ok {
			t.Fatalf("parseSessionRoute(%q) = (%q, %q, %v)", test.path, sid, action, ok)
		}
	}
}
