import CryptoKit
import Foundation

@_cdecl("VirContentEd25519Verify")
public func VirContentEd25519Verify(
	_ publicKey: UnsafePointer<UInt8>?,
	_ publicKeyLength: Int,
	_ message: UnsafePointer<UInt8>?,
	_ messageLength: Int,
	_ signature: UnsafePointer<UInt8>?,
	_ signatureLength: Int
) -> Bool {
	guard publicKeyLength == 32, signatureLength == 64,
		  let publicKey, let signature,
		  messageLength == 0 || message != nil else {
		return false
	}
	do {
		let keyData = Data(bytes: publicKey, count: publicKeyLength)
		let messageData = messageLength == 0 ? Data() : Data(bytes: message!, count: messageLength)
		let signatureData = Data(bytes: signature, count: signatureLength)
		let key = try Curve25519.Signing.PublicKey(rawRepresentation: keyData)
		return key.isValidSignature(signatureData, for: messageData)
	} catch {
		return false
	}
}
