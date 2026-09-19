FROM golang:1.25.0-alpine AS build
WORKDIR /src
COPY go.mod go.sum ./
COPY cmd ./cmd
RUN CGO_ENABLED=0 go build -trimpath -ldflags='-s -w' -o /api ./cmd/api && CGO_ENABLED=0 go build -trimpath -ldflags='-s -w' -o /worker ./cmd/worker
FROM gcr.io/distroless/static-debian12:nonroot
COPY --from=build /api /api
COPY --from=build /worker /worker
EXPOSE 8080
ENTRYPOINT ["/api"]
