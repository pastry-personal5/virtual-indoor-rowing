#include "OnlineClient/OnlineClient.h"

#import <Foundation/Foundation.h>

#include <dispatch/dispatch.h>

#include <cstdlib>

namespace OnlineClient
{
	namespace
	{
		NSString *String(const std::string &Value)
		{
			return [NSString stringWithUTF8String:Value.c_str()];
		}
		std::string StdString(id Value)
		{
			return [Value isKindOfClass:[NSString class]] ? std::string([Value UTF8String]) : std::string();
		}
		NSString *Path(const std::string &Base, const char *Suffix)
		{
			std::string Url = Base;
			while (!Url.empty() && Url.back() == '/')
				Url.pop_back();
			Url += Suffix;
			return String(Url);
		}

		NSData *Perform(NSString *Url, NSString *Method, NSData *Body, NSDictionary *Headers, NSInteger &Status)
		{
			NSMutableURLRequest *Request = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:Url]];
			Request.HTTPMethod = Method;
			Request.HTTPBody = Body;
			for (NSString *Key in Headers)
				[Request setValue:Headers[Key] forHTTPHeaderField:Key];
			__block NSData *ResponseData = nil;
			__block NSInteger ResponseStatus = 0;
			dispatch_semaphore_t Semaphore = dispatch_semaphore_create(0);
			NSURLSessionDataTask *Task = [[NSURLSession sharedSession] dataTaskWithRequest:Request
																		 completionHandler:^(NSData *Data, NSURLResponse *Response, NSError *) {
																		   ResponseData = Data;
																		   ResponseStatus = [(NSHTTPURLResponse *)Response statusCode];
																		   dispatch_semaphore_signal(Semaphore);
																		 }];
			[Task resume];
			dispatch_semaphore_wait(Semaphore, DISPATCH_TIME_FOREVER);
			Status = ResponseStatus;
			return ResponseData;
		}

		NSDictionary *Json(NSData *Data)
		{
			if (Data == nil)
				return nil;
			id Value = [NSJSONSerialization JSONObjectWithData:Data options:0 error:nil];
			return [Value isKindOfClass:[NSDictionary class]] ? Value : nil;
		}
		NSDictionary *Auth(const std::string &Token)
		{
			return @{@"Authorization" : [NSString stringWithFormat:@"Bearer %s", Token.c_str()]};
		}
		std::string CanonicalPath(const FRowingSessionId &Id)
		{
			return "/v1/sessions/" + Id.ToCanonicalString();
		}
	} // namespace

	struct FMacHttpTransport::FImpl
	{
		std::string BaseUrl;
		explicit FImpl(std::string InBaseUrl)
			: BaseUrl(std::move(InBaseUrl)) {}
	};
	FMacHttpTransport::FMacHttpTransport(std::string InBaseUrl)
		: Impl(std::make_unique<FImpl>(std::move(InBaseUrl))) {}
	FMacHttpTransport::~FMacHttpTransport() = default;

	std::string FMacHttpTransport::Bootstrap(const std::string &BootstrapSecret, std::string &Identity, std::chrono::system_clock::time_point &ExpiresAt)
	{
		NSDictionary *Request = @{@"request_version" : @1,
								  @"bootstrap_secret" : String(BootstrapSecret)};
		NSData *Body = [NSJSONSerialization dataWithJSONObject:Request options:0 error:nil];
		NSInteger Status = 0;
		NSDictionary *Response = Json(Perform(Path(Impl->BaseUrl, "/v1/development/bootstrap"), @"POST", Body, @{@"Content-Type" : @"application/json"}, Status));
		if (Status < 200 || Status >= 300 || Response == nil)
			return {};
		Identity = StdString(Response[@"development_identity"]);
		NSString *Expiry = Response[@"expires_at"];
		NSDate *Date = [[NSISO8601DateFormatter new] dateFromString:Expiry];
		if (Date == nil)
			return {};
		ExpiresAt = std::chrono::system_clock::time_point(std::chrono::seconds(static_cast<std::int64_t>(Date.timeIntervalSince1970)));
		return StdString(Response[@"bearer_token"]);
	}

	bool FMacHttpTransport::CreateSession(const std::string &Token, const FRowingSessionId &Id, const std::string &Disposition, const std::string &Digest, const std::string &IdempotencyKey)
	{
		NSDictionary *Request = @{@"request_version" : @1,
								  @"disposition" : String(Disposition),
								  @"object_sha256" : String(Digest)};
		NSData *Body = [NSJSONSerialization dataWithJSONObject:Request options:0 error:nil];
		NSInteger Status = 0;
		NSMutableDictionary *Headers = [Auth(Token) mutableCopy];
		Headers[@"Content-Type"] = @"application/json";
		Headers[@"Idempotency-Key"] = String(IdempotencyKey);
		Perform(Path(Impl->BaseUrl, CanonicalPath(Id).c_str()), @"PUT", Body, Headers, Status);
		return Status >= 200 && Status < 300;
	}

	bool FMacHttpTransport::FinalizeSession(const std::string &Token, const FRowingSessionId &Id, const std::string &Digest, const std::string &IdempotencyKey)
	{
		NSDictionary *Request = @{@"request_version" : @1,
								  @"object_sha256" : String(Digest)};
		NSData *Body = [NSJSONSerialization dataWithJSONObject:Request options:0 error:nil];
		NSInteger Status = 0;
		NSMutableDictionary *Headers = [Auth(Token) mutableCopy];
		Headers[@"Content-Type"] = @"application/json";
		Headers[@"Idempotency-Key"] = String(IdempotencyKey);
		std::string Url = CanonicalPath(Id) + "/finalize";
		Perform(Path(Impl->BaseUrl, Url.c_str()), @"POST", Body, Headers, Status);
		return Status >= 200 && Status < 300;
	}

	FUploadGrant FMacHttpTransport::RequestUpload(const std::string &Token, const FRowingSessionId &Id)
	{
		NSInteger Status = 0;
		std::string Url = CanonicalPath(Id) + "/upload";
		NSDictionary *Response = Json(Perform(Path(Impl->BaseUrl, Url.c_str()), @"POST", nil, Auth(Token), Status));
		if (Status < 200 || Status >= 300 || Response == nil)
			return {};
		return {StdString(Response[@"upload_url"]), StdString(Response[@"expected_sha256"])};
	}

	bool FMacHttpTransport::UploadObject(const FUploadGrant &Grant, const std::string &ObjectBytes)
	{
		NSInteger Status = 0;
		Perform(String(Grant.UploadUrl), @"PUT", [NSData dataWithBytes:ObjectBytes.data() length:ObjectBytes.size()], @{@"Content-Type" : @"application/octet-stream"}, Status);
		return Status >= 200 && Status < 300;
	}

	bool FMacHttpTransport::CompleteUpload(const std::string &Token, const FRowingSessionId &Id)
	{
		NSInteger Status = 0;
		std::string Url = CanonicalPath(Id) + "/upload-complete";
		Perform(Path(Impl->BaseUrl, Url.c_str()), @"POST", nil, Auth(Token), Status);
		return Status >= 200 && Status < 300;
	}

	std::string FMacHttpTransport::ProcessingStatus(const std::string &Token, const FRowingSessionId &Id)
	{
		NSInteger Status = 0;
		std::string Url = CanonicalPath(Id) + "/processing-status";
		NSDictionary *Response = Json(Perform(Path(Impl->BaseUrl, Url.c_str()), @"GET", nil, Auth(Token), Status));
		if (Status < 200 || Status >= 300 || Response == nil)
			return {};
		return StdString(Response[@"status"]);
	}
} // namespace OnlineClient
