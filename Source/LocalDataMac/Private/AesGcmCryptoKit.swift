import CryptoKit
import Foundation

// C-ABI shim over CryptoKit's Swift-only AES.GCM. Sealed layout is CryptoKit's
// `combined` form: 12-byte random nonce || ciphertext || 16-byte tag. Returns
// the number of bytes written, or -1 on any failure (including authentication).

// The key is handed to SymmetricKey straight from the caller's buffer, never via
// a Data copy, because SymmetricKey zeroizes its storage on release and Data does not.
private func makeData(_ pointer: UnsafePointer<UInt8>?, _ length: Int) -> Data {
	guard let pointer = pointer, length > 0 else { return Data() }
	return Data(bytes: pointer, count: length)
}

@_cdecl("LocalDataAesGcmSeal")
public func localDataAesGcmSeal(
	_ key: UnsafePointer<UInt8>,
	_ plaintext: UnsafePointer<UInt8>?, _ plaintextLength: Int,
	_ aad: UnsafePointer<UInt8>?, _ aadLength: Int,
	_ output: UnsafeMutablePointer<UInt8>, _ outputCapacity: Int
) -> Int {
	let symmetricKey = SymmetricKey(data: UnsafeRawBufferPointer(start: key, count: 32))
	guard
		let box = try? AES.GCM.seal(
			makeData(plaintext, plaintextLength), using: symmetricKey,
			authenticating: makeData(aad, aadLength)),
		let combined = box.combined,
		combined.count <= outputCapacity
	else { return -1 }
	combined.copyBytes(to: output, count: combined.count)
	return combined.count
}

@_cdecl("LocalDataAesGcmOpen")
public func localDataAesGcmOpen(
	_ key: UnsafePointer<UInt8>,
	_ sealed: UnsafePointer<UInt8>?, _ sealedLength: Int,
	_ aad: UnsafePointer<UInt8>?, _ aadLength: Int,
	_ output: UnsafeMutablePointer<UInt8>, _ outputCapacity: Int
) -> Int {
	let symmetricKey = SymmetricKey(data: UnsafeRawBufferPointer(start: key, count: 32))
	guard
		let box = try? AES.GCM.SealedBox(combined: makeData(sealed, sealedLength)),
		let plaintext = try? AES.GCM.open(
			box, using: symmetricKey, authenticating: makeData(aad, aadLength)),
		plaintext.count <= outputCapacity
	else { return -1 }
	plaintext.copyBytes(to: output, count: plaintext.count)
	return plaintext.count
}
