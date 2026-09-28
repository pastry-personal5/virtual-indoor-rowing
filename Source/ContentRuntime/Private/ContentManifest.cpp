#include "ContentRuntime/ContentManifest.h"

#include "rowing/v1/content.pb.h"

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace ContentRuntime
{
	namespace
	{
		constexpr std::array<std::uint32_t, 64> ShaConstants = {
			0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

		constexpr std::array<std::uint32_t, 8> ShaInitial = {
			0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU, 0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};

		std::uint32_t RotateRight(std::uint32_t Value, unsigned Count)
		{
			return (Value >> Count) | (Value << (32U - Count));
		}

		class FSha256Builder
		{
		  public:
			void Update(std::span<const std::uint8_t> Bytes)
			{
				TotalBytes += Bytes.size();
				while (!Bytes.empty())
				{
					const std::size_t Count = std::min(Bytes.size(), Block.size() - BlockBytes);
					std::memcpy(Block.data() + BlockBytes, Bytes.data(), Count);
					BlockBytes += Count;
					Bytes = Bytes.subspan(Count);
					if (BlockBytes == Block.size())
					{
						Transform();
						BlockBytes = 0;
					}
				}
			}

			FSha256 Finish()
			{
				const std::uint64_t BitCount = TotalBytes * 8ULL;
				Block[BlockBytes++] = 0x80;
				if (BlockBytes > 56)
				{
					std::fill(Block.begin() + static_cast<std::ptrdiff_t>(BlockBytes), Block.end(), 0);
					Transform();
					BlockBytes = 0;
				}
				std::fill(Block.begin() + static_cast<std::ptrdiff_t>(BlockBytes), Block.begin() + 56, 0);
				for (unsigned Index = 0; Index < 8; ++Index)
					Block[63 - Index] = static_cast<std::uint8_t>(BitCount >> (Index * 8U));
				Transform();
				FSha256 Result{};
				for (std::size_t Word = 0; Word < State.size(); ++Word)
				{
					for (unsigned Byte = 0; Byte < 4; ++Byte)
						Result[Word * 4 + Byte] = static_cast<std::uint8_t>(State[Word] >> (24U - Byte * 8U));
				}
				return Result;
			}

		  private:
			void Transform()
			{
				std::array<std::uint32_t, 64> Words{};
				for (std::size_t Index = 0; Index < 16; ++Index)
				{
					const std::size_t Offset = Index * 4;
					Words[Index] = (static_cast<std::uint32_t>(Block[Offset]) << 24U) |
								   (static_cast<std::uint32_t>(Block[Offset + 1]) << 16U) |
								   (static_cast<std::uint32_t>(Block[Offset + 2]) << 8U) |
								   static_cast<std::uint32_t>(Block[Offset + 3]);
				}
				for (std::size_t Index = 16; Index < Words.size(); ++Index)
				{
					const std::uint32_t S0 = RotateRight(Words[Index - 15], 7) ^ RotateRight(Words[Index - 15], 18) ^ (Words[Index - 15] >> 3U);
					const std::uint32_t S1 = RotateRight(Words[Index - 2], 17) ^ RotateRight(Words[Index - 2], 19) ^ (Words[Index - 2] >> 10U);
					Words[Index] = Words[Index - 16] + S0 + Words[Index - 7] + S1;
				}
				auto Working = State;
				for (std::size_t Index = 0; Index < Words.size(); ++Index)
				{
					const std::uint32_t S1 = RotateRight(Working[4], 6) ^ RotateRight(Working[4], 11) ^ RotateRight(Working[4], 25);
					const std::uint32_t Choice = (Working[4] & Working[5]) ^ (~Working[4] & Working[6]);
					const std::uint32_t Temp1 = Working[7] + S1 + Choice + ShaConstants[Index] + Words[Index];
					const std::uint32_t S0 = RotateRight(Working[0], 2) ^ RotateRight(Working[0], 13) ^ RotateRight(Working[0], 22);
					const std::uint32_t Majority = (Working[0] & Working[1]) ^ (Working[0] & Working[2]) ^ (Working[1] & Working[2]);
					const std::uint32_t Temp2 = S0 + Majority;
					Working[7] = Working[6];
					Working[6] = Working[5];
					Working[5] = Working[4];
					Working[4] = Working[3] + Temp1;
					Working[3] = Working[2];
					Working[2] = Working[1];
					Working[1] = Working[0];
					Working[0] = Temp1 + Temp2;
				}
				for (std::size_t Index = 0; Index < State.size(); ++Index)
					State[Index] += Working[Index];
			}

			std::array<std::uint32_t, 8> State = ShaInitial;
			std::array<std::uint8_t, 64> Block{};
			std::size_t BlockBytes = 0;
			std::uint64_t TotalBytes = 0;
		};

		template <typename TMessage>
		std::string DeterministicSerialize(const TMessage &Message)
		{
			std::string Bytes;
			Bytes.resize(Message.ByteSizeLong());
			google::protobuf::io::ArrayOutputStream Array(Bytes.data(), static_cast<int>(Bytes.size()));
			google::protobuf::io::CodedOutputStream Coded(&Array);
			Coded.SetSerializationDeterministic(true);
			if (!Message.SerializeToCodedStream(&Coded) || Coded.HadError())
				throw FContentValidationError(EContentError::Malformed, "manifest serialization failed");
			Bytes.resize(static_cast<std::size_t>(Coded.ByteCount()));
			return Bytes;
		}

		bool IsIdentifier(std::string_view Value, std::size_t Maximum)
		{
			if (Value.empty() || Value.size() > Maximum)
				return false;
			return std::all_of(Value.begin(), Value.end(), [](unsigned char Character)
							   { return std::isalnum(Character) != 0 || Character == '.' || Character == '-' || Character == '_'; });
		}

		bool IsLocalizationKey(std::string_view Value)
		{
			return IsIdentifier(Value, 128);
		}

		FSha256 RequireHash(std::string_view Bytes, std::string_view Name)
		{
			const auto Hash = ParseSha256(Bytes);
			if (!Hash)
				throw FContentValidationError(EContentError::InvalidHash, std::string(Name) + " must be 32 bytes");
			return *Hash;
		}

		FClientCompatibility ConvertCompatibility(const rowing::v1::ClientCompatibilityV1 &Wire)
		{
			return {Wire.minimum_build(), Wire.maximum_build(), Wire.content_schema(), Wire.route_schema()};
		}

		void ValidateCompatibility(const FClientCompatibility &Compatibility, std::uint32_t ClientBuild)
		{
			if (Compatibility.MinimumBuild == 0 || Compatibility.MaximumBuild < Compatibility.MinimumBuild ||
				Compatibility.ContentSchema != ContentManifestSchemaV1)
				throw FContentValidationError(EContentError::BadSchema, "invalid compatibility range or schema");
			if (Compatibility.RouteSchema != RouteDefinitionSchemaV2)
				throw FContentValidationError(EContentError::RouteSchemaIncompatible, "content.route_schema_incompatible");
			if (!Compatibility.Supports(ClientBuild))
				throw FContentValidationError(EContentError::Incompatible, "manifest is incompatible with this client build");
		}

		FRouteVectorMm ConvertVector(const rowing::v1::RouteVectorMmV2 &Wire)
		{
			return {Wire.x_mm(), Wire.y_mm(), Wire.z_mm()};
		}

		double Length3(const FRouteVectorMm &Value)
		{
			return std::hypot(std::hypot(static_cast<double>(Value.X), static_cast<double>(Value.Y)), static_cast<double>(Value.Z));
		}

		struct FDoubleVector
		{
			double X = 0.0;
			double Y = 0.0;
			double Z = 0.0;
		};

		FDoubleVector EvaluateHermite(const FRouteHermiteControlPoint &Start,
									  const FRouteHermiteControlPoint &End,
									  double T)
		{
			const double T2 = T * T;
			const double T3 = T2 * T;
			const double H00 = 2.0 * T3 - 3.0 * T2 + 1.0;
			const double H10 = T3 - 2.0 * T2 + T;
			const double H01 = -2.0 * T3 + 3.0 * T2;
			const double H11 = T3 - T2;
			return {
				H00 * Start.PositionMm.X + H10 * Start.LeaveTangentMm.X + H01 * End.PositionMm.X + H11 * End.ArriveTangentMm.X,
				H00 * Start.PositionMm.Y + H10 * Start.LeaveTangentMm.Y + H01 * End.PositionMm.Y + H11 * End.ArriveTangentMm.Y,
				H00 * Start.PositionMm.Z + H10 * Start.LeaveTangentMm.Z + H01 * End.PositionMm.Z + H11 * End.ArriveTangentMm.Z};
		}

		FDoubleVector EvaluateHermiteDerivative(const FRouteHermiteControlPoint &Start,
												const FRouteHermiteControlPoint &End,
												double T)
		{
			const double T2 = T * T;
			const double H00 = 6.0 * T2 - 6.0 * T;
			const double H10 = 3.0 * T2 - 4.0 * T + 1.0;
			const double H01 = -6.0 * T2 + 6.0 * T;
			const double H11 = 3.0 * T2 - 2.0 * T;
			return {
				H00 * Start.PositionMm.X + H10 * Start.LeaveTangentMm.X + H01 * End.PositionMm.X + H11 * End.ArriveTangentMm.X,
				H00 * Start.PositionMm.Y + H10 * Start.LeaveTangentMm.Y + H01 * End.PositionMm.Y + H11 * End.ArriveTangentMm.Y,
				H00 * Start.PositionMm.Z + H10 * Start.LeaveTangentMm.Z + H01 * End.PositionMm.Z + H11 * End.ArriveTangentMm.Z};
		}

		FDoubleVector EvaluateHermiteSecondDerivative(const FRouteHermiteControlPoint &Start,
													  const FRouteHermiteControlPoint &End,
													  double T)
		{
			const double H00 = 12.0 * T - 6.0;
			const double H10 = 6.0 * T - 4.0;
			const double H01 = -12.0 * T + 6.0;
			const double H11 = 6.0 * T - 2.0;
			return {
				H00 * Start.PositionMm.X + H10 * Start.LeaveTangentMm.X + H01 * End.PositionMm.X + H11 * End.ArriveTangentMm.X,
				H00 * Start.PositionMm.Y + H10 * Start.LeaveTangentMm.Y + H01 * End.PositionMm.Y + H11 * End.ArriveTangentMm.Y,
				H00 * Start.PositionMm.Z + H10 * Start.LeaveTangentMm.Z + H01 * End.PositionMm.Z + H11 * End.ArriveTangentMm.Z};
		}

		double Distance(const FDoubleVector &A, const FDoubleVector &B)
		{
			return std::hypot(std::hypot(A.X - B.X, A.Y - B.Y), A.Z - B.Z);
		}

		double Cross2d(const FDoubleVector &Origin, const FDoubleVector &A, const FDoubleVector &B)
		{
			return (A.X - Origin.X) * (B.Y - Origin.Y) - (A.Y - Origin.Y) * (B.X - Origin.X);
		}

		bool IsBetween(double Value, double A, double B)
		{
			return Value >= std::min(A, B) - 1e-6 && Value <= std::max(A, B) + 1e-6;
		}

		bool SegmentsIntersect2d(const FDoubleVector &A, const FDoubleVector &B, const FDoubleVector &C, const FDoubleVector &D)
		{
			const double Abc = Cross2d(A, B, C);
			const double Abd = Cross2d(A, B, D);
			const double Cda = Cross2d(C, D, A);
			const double Cdb = Cross2d(C, D, B);
			const bool bAbStraddles = (Abc > 0.0 && Abd < 0.0) || (Abc < 0.0 && Abd > 0.0);
			const bool bCdStraddles = (Cda > 0.0 && Cdb < 0.0) || (Cda < 0.0 && Cdb > 0.0);
			const bool bProperCrossing = bAbStraddles && bCdStraddles;
			if (bProperCrossing)
				return true;
			return (std::abs(Abc) <= 1e-6 && IsBetween(C.X, A.X, B.X) && IsBetween(C.Y, A.Y, B.Y)) ||
				   (std::abs(Abd) <= 1e-6 && IsBetween(D.X, A.X, B.X) && IsBetween(D.Y, A.Y, B.Y)) ||
				   (std::abs(Cda) <= 1e-6 && IsBetween(A.X, C.X, D.X) && IsBetween(A.Y, C.Y, D.Y)) ||
				   (std::abs(Cdb) <= 1e-6 && IsBetween(B.X, C.X, D.X) && IsBetween(B.Y, C.Y, D.Y));
		}

		void ValidatePresentationPath(FRouteDefinition &Route, const rowing::v1::RoutePresentationPathV2 &Wire)
		{
			FRoutePresentationPath Path;
			Path.PathFormatVersion = Wire.path_format_version();
			Path.OwningRouteId = Wire.owning_route_id();
			Path.RouteLocalOriginMm = ConvertVector(Wire.route_local_origin_mm());
			Path.RouteLocalYawMicroradians = Wire.route_local_yaw_microradians();
			Path.ArcLengthLookupSha256 = RequireHash(Wire.arc_length_lookup_sha256(), "arc-length lookup hash");
			if (Path.PathFormatVersion != PresentationPathFormatV1 || Path.OwningRouteId != Route.RouteId ||
				std::abs(static_cast<std::int64_t>(Path.RouteLocalYawMicroradians)) > 3'141'593)
				throw FContentValidationError(EContentError::InvalidRoute, "presentation path frame or owner is invalid");
			if (Wire.control_points_size() < 8 || Wire.control_points_size() > 40)
				throw FContentValidationError(EContentError::InvalidRoute, "presentation path requires 8-40 control points");
			std::unordered_set<std::string> PointIds;
			std::uint64_t PreviousDistance = 0;
			for (int Index = 0; Index < Wire.control_points_size(); ++Index)
			{
				const auto &PointWire = Wire.control_points(Index);
				FRouteHermiteControlPoint Point;
				Point.PointId = PointWire.point_id();
				Point.RouteDistanceMm = PointWire.route_distance_mm();
				Point.PositionMm = ConvertVector(PointWire.position_mm());
				Point.ArriveTangentMm = ConvertVector(PointWire.arrive_tangent_mm());
				Point.LeaveTangentMm = ConvertVector(PointWire.leave_tangent_mm());
				const bool bEndpointValid = (Index == 0 && Point.RouteDistanceMm == 0) ||
											(Index > 0 && Point.RouteDistanceMm > PreviousDistance);
				if (!IsIdentifier(Point.PointId, 128) || !PointIds.insert(Point.PointId).second || !bEndpointValid ||
					std::abs(Point.PositionMm.X) > 10'000'000 || std::abs(Point.PositionMm.Y) > 10'000'000 ||
					std::abs(Point.PositionMm.Z) > 1'000'000 || Length3(Point.ArriveTangentMm) < 1.0 ||
					Length3(Point.LeaveTangentMm) < 1.0 || Length3(Point.ArriveTangentMm) > 20'000'000.0 ||
					Length3(Point.LeaveTangentMm) > 20'000'000.0)
					throw FContentValidationError(EContentError::InvalidRoute, "presentation path control point is invalid");
				PreviousDistance = Point.RouteDistanceMm;
				Path.ControlPoints.push_back(std::move(Point));
			}
			if (Path.ControlPoints.back().RouteDistanceMm != Route.LengthMm)
				throw FContentValidationError(EContentError::InvalidRoute, "presentation path endpoints do not match route length");

			// C1 direction continuity is explicit. Different tangent magnitudes are
			// allowed because they tune the adjacent Hermite spans.
			for (std::size_t Index = 1; Index + 1 < Path.ControlPoints.size(); ++Index)
			{
				const auto &Point = Path.ControlPoints[Index];
				const double Dot = static_cast<double>(Point.ArriveTangentMm.X) * Point.LeaveTangentMm.X +
								   static_cast<double>(Point.ArriveTangentMm.Y) * Point.LeaveTangentMm.Y +
								   static_cast<double>(Point.ArriveTangentMm.Z) * Point.LeaveTangentMm.Z;
				if (Dot / (Length3(Point.ArriveTangentMm) * Length3(Point.LeaveTangentMm)) < 0.999)
					throw FContentValidationError(EContentError::InvalidRoute, "presentation path tangent continuity failed");
			}

			if (Wire.arc_length_lookup_size() < 2 || Wire.arc_length_lookup_size() > 1'000'002)
				throw FContentValidationError(EContentError::Oversized, "presentation path lookup size is invalid");
			std::uint64_t PreviousLookupDistance = 0;
			std::uint32_t PreviousSegment = 0;
			std::uint32_t PreviousParameter = 0;
			for (int Index = 0; Index < Wire.arc_length_lookup_size(); ++Index)
			{
				const auto &EntryWire = Wire.arc_length_lookup(Index);
				FRouteArcLengthLookupEntry Entry{EntryWire.route_distance_mm(), EntryWire.segment_index(), EntryWire.segment_parameter_ppm()};
				const bool bFirst = Index == 0;
				const bool bLast = Index + 1 == Wire.arc_length_lookup_size();
				const bool bCurveMonotonic = bFirst || Entry.SegmentIndex > PreviousSegment ||
											 (Entry.SegmentIndex == PreviousSegment && Entry.SegmentParameterPpm > PreviousParameter);
				if (Entry.SegmentIndex + 1 >= Path.ControlPoints.size() || Entry.SegmentParameterPpm > 1'000'000 ||
					(bFirst && (Entry.RouteDistanceMm != 0 || Entry.SegmentIndex != 0 || Entry.SegmentParameterPpm != 0)) ||
					(!bFirst && (Entry.RouteDistanceMm <= PreviousLookupDistance || Entry.RouteDistanceMm - PreviousLookupDistance > 5'000 || !bCurveMonotonic)) ||
					(bLast && (Entry.RouteDistanceMm != Route.LengthMm || Entry.SegmentIndex + 2 != Path.ControlPoints.size() || Entry.SegmentParameterPpm != 1'000'000)))
					throw FContentValidationError(EContentError::InvalidRoute, "presentation path lookup is not bounded and monotonic");
				PreviousLookupDistance = Entry.RouteDistanceMm;
				PreviousSegment = Entry.SegmentIndex;
				PreviousParameter = Entry.SegmentParameterPpm;
				Path.ArcLengthLookup.push_back(Entry);
			}
			const std::string LookupBytes = CanonicalArcLengthLookupBytes(Path.ArcLengthLookup);
			if (Sha256(std::span(reinterpret_cast<const std::uint8_t *>(LookupBytes.data()), LookupBytes.size())) != Path.ArcLengthLookupSha256)
				throw FContentValidationError(EContentError::InvalidHash, "arc-length lookup hash does not match canonical bytes");

			// Independent dense audit: 128 chords per segment is deterministic and
			// substantially denser than the signed <=5 m lookup.
			double DenseLengthMm = 0.0;
			std::vector<FDoubleVector> DensePoints;
			for (std::size_t Segment = 0; Segment + 1 < Path.ControlPoints.size(); ++Segment)
			{
				const auto &Start = Path.ControlPoints[Segment];
				const auto &End = Path.ControlPoints[Segment + 1];
				FDoubleVector Previous = EvaluateHermite(Start, End, 0.0);
				if (DensePoints.empty())
					DensePoints.push_back(Previous);
				const FDoubleVector Chord{static_cast<double>(End.PositionMm.X - Start.PositionMm.X),
										  static_cast<double>(End.PositionMm.Y - Start.PositionMm.Y),
										  static_cast<double>(End.PositionMm.Z - Start.PositionMm.Z)};
				for (int Step = 1; Step <= 128; ++Step)
				{
					const double T = static_cast<double>(Step) / 128.0;
					const FDoubleVector Current = EvaluateHermite(Start, End, T);
					DenseLengthMm += Distance(Previous, Current);
					Previous = Current;
					const FDoubleVector D1 = EvaluateHermiteDerivative(Start, End, T);
					const FDoubleVector D2 = EvaluateHermiteSecondDerivative(Start, End, T);
					const double SpeedSquared = D1.X * D1.X + D1.Y * D1.Y;
					const double Cross = std::abs(D1.X * D2.Y - D1.Y * D2.X);
					const double ForwardDotChord = D1.X * Chord.X + D1.Y * Chord.Y + D1.Z * Chord.Z;
					if (SpeedSquared < 1.0 ||
						(Cross > 1e-9 && std::pow(SpeedSquared, 1.5) / Cross < 100'000.0))
						throw FContentValidationError(EContentError::InvalidRoute, "presentation path turn radius is below 100 m");
					if (ForwardDotChord <= 0.0)
						throw FContentValidationError(EContentError::InvalidRoute, "presentation path reverses within a span");
					DensePoints.push_back(Current);
				}
			}
			for (std::size_t First = 0; First + 1 < DensePoints.size(); ++First)
			{
				for (std::size_t Second = First + 2; Second + 1 < DensePoints.size(); ++Second)
				{
					if (SegmentsIntersect2d(DensePoints[First], DensePoints[First + 1], DensePoints[Second], DensePoints[Second + 1]))
						throw FContentValidationError(EContentError::InvalidRoute, "presentation path self-intersects");
				}
			}
			if (std::abs(DenseLengthMm - static_cast<double>(Route.LengthMm)) > 500.0)
				throw FContentValidationError(EContentError::InvalidRoute, "presentation path dense length audit exceeds 500 mm tolerance");
			Route.PresentationPath = std::move(Path);
		}

		FRouteDefinition ConvertRoute(const rowing::v1::RouteDefinitionV1 &Wire, std::uint32_t ClientBuild)
		{
			FRouteDefinition Route;
			Route.SchemaVersion = Wire.schema_version();
			Route.RouteId = Wire.route_id();
			Route.SemanticVersion = Wire.semantic_version();
			Route.ContentSetId = Wire.content_set_id();
			Route.LengthMm = Wire.length_mm();
			Route.bClosed = Wire.is_closed();
			Route.Compatibility = ConvertCompatibility(Wire.compatibility());
			Route.DisplayNameKey = Wire.display_name_key();
			Route.DescriptionKey = Wire.description_key();
			Route.MetadataSha256 = RequireHash(Wire.metadata_sha256(), "route metadata hash");
			if (Route.SchemaVersion != RouteDefinitionSchemaV2)
				throw FContentValidationError(EContentError::RouteSchemaIncompatible, "content.route_schema_incompatible");
			if (!IsIdentifier(Route.RouteId, 128) ||
				!IsIdentifier(Route.SemanticVersion, 64) || !IsIdentifier(Route.ContentSetId, 128) ||
				Route.LengthMm == 0 || Route.LengthMm > 1'000'000'000ULL ||
				!IsLocalizationKey(Route.DisplayNameKey) || !IsLocalizationKey(Route.DescriptionKey))
				throw FContentValidationError(EContentError::InvalidRoute, "route definition fields are invalid");
			ValidateCompatibility(Route.Compatibility, ClientBuild);
			if (Wire.checkpoints_size() > 1024)
				throw FContentValidationError(EContentError::Oversized, "too many route checkpoints");
			std::uint64_t PreviousDistance = 0;
			for (const auto &Checkpoint : Wire.checkpoints())
			{
				if (!IsIdentifier(Checkpoint.checkpoint_id(), 128) || Checkpoint.distance_mm() <= PreviousDistance || Checkpoint.distance_mm() >= Route.LengthMm)
					throw FContentValidationError(EContentError::InvalidRoute, "route checkpoints must be named, ordered, and inside the route");
				Route.Checkpoints.push_back({Checkpoint.checkpoint_id(), Checkpoint.distance_mm()});
				PreviousDistance = Checkpoint.distance_mm();
			}
			if (!Wire.has_presentation_path())
				throw FContentValidationError(EContentError::InvalidRoute, "route-schema v2 requires a presentation path");
			ValidatePresentationPath(Route, Wire.presentation_path());
			rowing::v1::RouteDefinitionV1 Hashable = Wire;
			Hashable.clear_metadata_sha256();
			const std::string HashableBytes = DeterministicSerialize(Hashable);
			if (Sha256(std::span(reinterpret_cast<const std::uint8_t *>(HashableBytes.data()), HashableBytes.size())) != Route.MetadataSha256)
				throw FContentValidationError(EContentError::InvalidHash, "route metadata hash does not match its canonical definition");
			return Route;
		}

		bool IsValidHttpsUrl(std::string_view Url, const FRouteDefinition &Route)
		{
			if (!Url.starts_with("https://") || Url.size() > 2048 || Url.find('\\') != std::string_view::npos ||
				Url.find('#') != std::string_view::npos || Url.find('?') != std::string_view::npos || Url.find("..") != std::string_view::npos)
				return false;
			const std::string_view Rest = Url.substr(8);
			const std::size_t Slash = Rest.find('/');
			if (Slash == 0 || Slash == std::string_view::npos || Rest.substr(0, Slash).find('@') != std::string_view::npos)
				return false;
			return Url.find(Route.ContentSetId) != std::string_view::npos && Url.find(Route.SemanticVersion) != std::string_view::npos;
		}
	} // namespace

	bool FClientCompatibility::Supports(std::uint32_t ClientBuild) const noexcept
	{
		return ClientBuild >= MinimumBuild && ClientBuild <= MaximumBuild;
	}

	std::string CanonicalArcLengthLookupBytes(const std::vector<FRouteArcLengthLookupEntry> &Entries)
	{
		std::string Bytes("VIRPATHLOOKUP1\0", 15);
		auto Append = [&Bytes](std::uint64_t Value, unsigned Width)
		{
			for (unsigned Index = 0; Index < Width; ++Index)
				Bytes.push_back(static_cast<char>(Value >> (Index * 8U)));
		};
		Append(Entries.size(), 4);
		for (const FRouteArcLengthLookupEntry &Entry : Entries)
		{
			Append(Entry.RouteDistanceMm, 8);
			Append(Entry.SegmentIndex, 4);
			Append(Entry.SegmentParameterPpm, 4);
		}
		return Bytes;
	}

	FContentValidationError::FContentValidationError(EContentError InCode, std::string Message)
		: std::runtime_error(std::move(Message)), Code(InCode)
	{
	}

	EContentError FContentValidationError::GetCode() const noexcept
	{
		return Code;
	}

	std::string CanonicalizeManifestPayload(std::string_view UnsignedPayload)
	{
		if (UnsignedPayload.empty() || UnsignedPayload.size() > MaximumManifestBytes)
			throw FContentValidationError(EContentError::Oversized, "unsigned manifest payload has an invalid size");
		rowing::v1::ContentManifestUnsignedV1 Wire;
		if (!Wire.ParseFromArray(UnsignedPayload.data(), static_cast<int>(UnsignedPayload.size())) || !Wire.IsInitialized())
			throw FContentValidationError(EContentError::Malformed, "unsigned manifest payload is malformed");
		return DeterministicSerialize(Wire);
	}

	FContentManifest ParseAndVerifyManifest(std::string_view EnvelopeBytes,
											const std::vector<FTrustedContentKey> &TrustedKeys,
											const IContentSignatureVerifier &Verifier,
											const FManifestPolicy &Policy)
	{
		if (EnvelopeBytes.empty() || EnvelopeBytes.size() > MaximumManifestBytes)
			throw FContentValidationError(EContentError::Oversized, "manifest envelope has an invalid size");
		rowing::v1::ContentManifestEnvelopeV1 Envelope;
		if (!Envelope.ParseFromArray(EnvelopeBytes.data(), static_cast<int>(EnvelopeBytes.size())) || !Envelope.IsInitialized())
			throw FContentValidationError(EContentError::Malformed, "manifest envelope is malformed");
		if (Envelope.envelope_version() != ContentManifestEnvelopeV1 || Envelope.signature().size() != 64 || !IsIdentifier(Envelope.key_id(), 64))
			throw FContentValidationError(EContentError::BadSchema, "manifest envelope version, key, or signature length is invalid");
		const std::string CanonicalPayload = CanonicalizeManifestPayload(Envelope.unsigned_payload());
		if (CanonicalPayload != Envelope.unsigned_payload())
			throw FContentValidationError(EContentError::NonCanonical, "manifest payload is not canonical");
		const auto Key = std::find_if(TrustedKeys.begin(), TrustedKeys.end(), [&](const FTrustedContentKey &Candidate)
									  { return Candidate.KeyId == Envelope.key_id(); });
		if (Key == TrustedKeys.end())
			throw FContentValidationError(EContentError::UnknownKey, "manifest key is not trusted");
		const auto PayloadSpan = std::span(reinterpret_cast<const std::uint8_t *>(CanonicalPayload.data()), CanonicalPayload.size());
		const auto SignatureSpan = std::span(reinterpret_cast<const std::uint8_t *>(Envelope.signature().data()), Envelope.signature().size());
		if (!Verifier.Verify(Key->PublicKey, PayloadSpan, SignatureSpan))
			throw FContentValidationError(EContentError::BadSignature, "manifest signature verification failed");

		rowing::v1::ContentManifestUnsignedV1 Wire;
		if (!Wire.ParseFromString(CanonicalPayload))
			throw FContentValidationError(EContentError::Malformed, "verified manifest could not be parsed");
		if (Wire.schema_version() != ContentManifestSchemaV1 || Wire.catalog_revision() == 0)
			throw FContentValidationError(EContentError::BadSchema, "manifest schema or revision is invalid");
		if (Wire.catalog_revision() < Policy.MinimumCatalogRevision)
			throw FContentValidationError(EContentError::RevisionRollback, "catalog revision is older than the accepted revision");
		if (Wire.issued_at_unix_seconds() <= 0 || Wire.expires_at_unix_seconds() <= Wire.issued_at_unix_seconds() ||
			Wire.expires_at_unix_seconds() - Wire.issued_at_unix_seconds() > 7 * 24 * 60 * 60)
			throw FContentValidationError(EContentError::Expired, "manifest validity window is invalid");
		if (!Policy.bAllowExpired && Policy.NowUnixSeconds >= Wire.expires_at_unix_seconds())
			throw FContentValidationError(EContentError::Expired, "manifest has expired");
		if (Wire.withdrawn() && !Policy.bAllowWithdrawn)
			throw FContentValidationError(EContentError::Withdrawn, "route is withdrawn");

		FContentManifest Manifest;
		Manifest.CatalogRevision = Wire.catalog_revision();
		Manifest.IssuedAtUnixSeconds = Wire.issued_at_unix_seconds();
		Manifest.ExpiresAtUnixSeconds = Wire.expires_at_unix_seconds();
		Manifest.Compatibility = ConvertCompatibility(Wire.compatibility());
		ValidateCompatibility(Manifest.Compatibility, Policy.ClientBuild);
		Manifest.Route = ConvertRoute(Wire.route(), Policy.ClientBuild);
		Manifest.PackageUrl = Wire.package_url();
		if (!IsValidHttpsUrl(Manifest.PackageUrl, Manifest.Route))
			throw FContentValidationError(EContentError::InvalidUrl, "package URL is not an immutable HTTPS route URL");
		Manifest.PackageSha256 = RequireHash(Wire.package_sha256(), "package hash");
		Manifest.InventorySha256 = RequireHash(Wire.inventory_sha256(), "inventory hash");
		const std::string CanonicalRoute = DeterministicSerialize(Wire.route());
		Manifest.RouteDefinitionSha256 = Sha256(std::span(reinterpret_cast<const std::uint8_t *>(CanonicalRoute.data()), CanonicalRoute.size()));
		Manifest.CompressedSizeBytes = Wire.compressed_size_bytes();
		Manifest.UncompressedSizeBytes = Wire.uncompressed_size_bytes();
		if (Manifest.CompressedSizeBytes == 0 || Manifest.CompressedSizeBytes > MaximumCompressedPackageBytes ||
			Manifest.UncompressedSizeBytes < Manifest.CompressedSizeBytes || Manifest.UncompressedSizeBytes > MaximumCompressedPackageBytes * 4ULL)
			throw FContentValidationError(EContentError::InvalidSize, "package sizes exceed content policy");
		Manifest.bWithdrawn = Wire.withdrawn();
		Manifest.WithdrawalReasonKey = Wire.withdrawal_reason_key();
		if (Manifest.bWithdrawn && !IsLocalizationKey(Manifest.WithdrawalReasonKey))
			throw FContentValidationError(EContentError::Malformed, "withdrawal requires a safe reason key");
		Manifest.KeyId = Envelope.key_id();
		Manifest.CanonicalUnsignedPayload = CanonicalPayload;
		Manifest.ManifestSha256 = Sha256(std::span(reinterpret_cast<const std::uint8_t *>(EnvelopeBytes.data()), EnvelopeBytes.size()));
		return Manifest;
	}

	FRouteDefinition BuiltInStandardRouteDefinition()
	{
		FRouteDefinition Route;
		Route.SchemaVersion = RouteDefinitionSchemaV1;
		Route.RouteId = "route.standard.2k";
		Route.SemanticVersion = "1.0.0";
		Route.ContentSetId = "builtin-standard";
		Route.LengthMm = 2'000'000;
		Route.bClosed = true;
		Route.Checkpoints = {{"km1", 1'000'000}};
		Route.Compatibility = {1, std::numeric_limits<std::uint32_t>::max(), ContentManifestSchemaV1, RouteDefinitionSchemaV1};
		Route.DisplayNameKey = "route.standard.name";
		Route.DescriptionKey = "route.standard.description";
		constexpr std::string_view CanonicalIdentity = "route.standard.2k|1.0.0|builtin-standard|2000000|closed|km1:1000000";
		Route.MetadataSha256 = Sha256(std::span(reinterpret_cast<const std::uint8_t *>(CanonicalIdentity.data()), CanonicalIdentity.size()));

		// The sole schema-v1 path exception is compiled into the client. It is the
		// same closed 32-span ellipse used by the Phase 1 actor, now expressed as
		// explicit Hermite data so Unreal no longer owns its parameterization.
		FRoutePresentationPath Path;
		Path.PathFormatVersion = PresentationPathFormatV1;
		Path.OwningRouteId = Route.RouteId;
		constexpr std::size_t SegmentCount = 32;
		constexpr double RadiusXmm = 650'000.0;
		constexpr double RadiusYmm = 180'000.0;
		constexpr double TwoPi = 6.283185307179586476925286766559;
		for (std::size_t Index = 0; Index <= SegmentCount; ++Index)
		{
			const double Angle = TwoPi * static_cast<double>(Index) / static_cast<double>(SegmentCount);
			const double SpanAngle = TwoPi / static_cast<double>(SegmentCount);
			FRouteHermiteControlPoint Point;
			Point.PointId = Index == SegmentCount ? "standard-finish" : "standard-" + std::to_string(Index);
			Point.RouteDistanceMm = Route.LengthMm * Index / SegmentCount;
			Point.PositionMm = {static_cast<std::int64_t>(std::llround(RadiusXmm * std::cos(Angle))),
								static_cast<std::int64_t>(std::llround(RadiusYmm * std::sin(Angle))),
								200};
			Point.ArriveTangentMm = {static_cast<std::int64_t>(std::llround(-RadiusXmm * std::sin(Angle) * SpanAngle)),
									 static_cast<std::int64_t>(std::llround(RadiusYmm * std::cos(Angle) * SpanAngle)),
									 0};
			Point.LeaveTangentMm = Point.ArriveTangentMm;
			Path.ControlPoints.push_back(std::move(Point));
		}

		struct FArcSample
		{
			double ArcMm;
			std::uint32_t Segment;
			double T;
		};
		std::vector<FArcSample> Samples;
		Samples.push_back({0.0, 0, 0.0});
		double TotalArcMm = 0.0;
		for (std::size_t Segment = 0; Segment < SegmentCount; ++Segment)
		{
			FDoubleVector Previous = EvaluateHermite(Path.ControlPoints[Segment], Path.ControlPoints[Segment + 1], 0.0);
			for (int Step = 1; Step <= 256; ++Step)
			{
				const double T = static_cast<double>(Step) / 256.0;
				const FDoubleVector Current = EvaluateHermite(Path.ControlPoints[Segment], Path.ControlPoints[Segment + 1], T);
				TotalArcMm += Distance(Previous, Current);
				Samples.push_back({TotalArcMm, static_cast<std::uint32_t>(Segment), T});
				Previous = Current;
			}
		}
		for (std::uint64_t DistanceMm = 0;; DistanceMm = std::min(Route.LengthMm, DistanceMm + 5'000))
		{
			if (DistanceMm == Route.LengthMm)
			{
				Path.ArcLengthLookup.push_back({DistanceMm, SegmentCount - 1, 1'000'000});
				break;
			}
			const double TargetArc = TotalArcMm * static_cast<double>(DistanceMm) / static_cast<double>(Route.LengthMm);
			const auto Upper = std::lower_bound(Samples.begin(), Samples.end(), TargetArc, [](const FArcSample &Sample, double Arc)
												{ return Sample.ArcMm < Arc; });
			const FArcSample &After = Upper == Samples.end() ? Samples.back() : *Upper;
			const FArcSample &Before = Upper == Samples.begin() ? *Upper : *(Upper - 1);
			double T = After.T;
			std::uint32_t Segment = After.Segment;
			if (After.Segment == Before.Segment && After.ArcMm > Before.ArcMm)
				T = Before.T + (After.T - Before.T) * (TargetArc - Before.ArcMm) / (After.ArcMm - Before.ArcMm);
			Path.ArcLengthLookup.push_back({DistanceMm, Segment, static_cast<std::uint32_t>(std::llround(std::clamp(T, 0.0, 1.0) * 1'000'000.0))});
		}
		const std::string LookupBytes = CanonicalArcLengthLookupBytes(Path.ArcLengthLookup);
		Path.ArcLengthLookupSha256 = Sha256(std::span(reinterpret_cast<const std::uint8_t *>(LookupBytes.data()), LookupBytes.size()));
		Route.PresentationPath = std::move(Path);
		return Route;
	}

	FRouteDefinition ParseAndValidateRouteDefinition(std::string_view RouteBytes, std::uint32_t ClientBuild)
	{
		if (RouteBytes.empty() || RouteBytes.size() > MaximumManifestBytes)
			throw FContentValidationError(EContentError::Oversized, "route definition has an invalid size");
		rowing::v1::RouteDefinitionV1 Wire;
		if (!Wire.ParseFromArray(RouteBytes.data(), static_cast<int>(RouteBytes.size())) || !Wire.IsInitialized())
			throw FContentValidationError(EContentError::Malformed, "route definition is malformed");
		if (DeterministicSerialize(Wire) != RouteBytes)
			throw FContentValidationError(EContentError::NonCanonical, "route definition is not canonical");
		return ConvertRoute(Wire, ClientBuild);
	}

	FSha256 Sha256(std::span<const std::uint8_t> Bytes)
	{
		FSha256Builder Builder;
		Builder.Update(Bytes);
		return Builder.Finish();
	}

	FSha256 Sha256File(const std::filesystem::path &Path, std::uint64_t MaximumBytes)
	{
		std::ifstream Input(Path, std::ios::binary);
		if (!Input)
			throw FContentValidationError(EContentError::IoFailure, "cannot open content file: " + Path.string());
		FSha256Builder Builder;
		std::array<std::uint8_t, 1024 * 1024> Buffer{};
		std::uint64_t Total = 0;
		while (Input)
		{
			Input.read(reinterpret_cast<char *>(Buffer.data()), static_cast<std::streamsize>(Buffer.size()));
			const auto Count = static_cast<std::size_t>(Input.gcount());
			Total += Count;
			if (Total > MaximumBytes)
				throw FContentValidationError(EContentError::Oversized, "content file exceeds declared bound: " + Path.string());
			Builder.Update(std::span(Buffer.data(), Count));
		}
		if (!Input.eof())
			throw FContentValidationError(EContentError::IoFailure, "cannot read content file: " + Path.string());
		return Builder.Finish();
	}

	std::string Sha256Hex(const FSha256 &Hash)
	{
		constexpr char Hex[] = "0123456789abcdef";
		std::string Result;
		Result.reserve(64);
		for (const std::uint8_t Byte : Hash)
		{
			Result.push_back(Hex[Byte >> 4U]);
			Result.push_back(Hex[Byte & 0x0FU]);
		}
		return Result;
	}

	std::optional<FSha256> ParseSha256(std::string_view Bytes)
	{
		if (Bytes.size() != 32)
			return std::nullopt;
		FSha256 Hash{};
		std::memcpy(Hash.data(), Bytes.data(), Hash.size());
		return Hash;
	}

	const char *ContentErrorName(EContentError Error) noexcept
	{
		switch (Error)
		{
		case EContentError::None:
			return "none";
		case EContentError::Malformed:
			return "malformed";
		case EContentError::Oversized:
			return "oversized";
		case EContentError::NonCanonical:
			return "non_canonical";
		case EContentError::UnknownKey:
			return "unknown_key";
		case EContentError::BadSignature:
			return "bad_signature";
		case EContentError::BadSchema:
			return "bad_schema";
		case EContentError::Incompatible:
			return "incompatible";
		case EContentError::RouteSchemaIncompatible:
			return "content.route_schema_incompatible";
		case EContentError::Expired:
			return "expired";
		case EContentError::RevisionRollback:
			return "revision_rollback";
		case EContentError::Withdrawn:
			return "withdrawn";
		case EContentError::InvalidRoute:
			return "invalid_route";
		case EContentError::InvalidUrl:
			return "invalid_url";
		case EContentError::InvalidHash:
			return "invalid_hash";
		case EContentError::InvalidSize:
			return "invalid_size";
		case EContentError::InvalidPath:
			return "invalid_path";
		case EContentError::DisallowedAssetClass:
			return "disallowed_asset_class";
		case EContentError::MissingLicenseNotice:
			return "missing_license_notice";
		case EContentError::InvalidLicenseProvenance:
			return "invalid_license_provenance";
		case EContentError::InventoryMismatch:
			return "inventory_mismatch";
		case EContentError::PackageMismatch:
			return "package_mismatch";
		case EContentError::StorageInsufficient:
			return "storage_insufficient";
		case EContentError::WorkoutActive:
			return "workout_active";
		case EContentError::IoFailure:
			return "io_failure";
		}
		return "unknown";
	}
} // namespace ContentRuntime
