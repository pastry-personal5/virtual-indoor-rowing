
export PB_PREFIX=catalog-b98eee6ad3b15a78703bc8c782fb65e1cb6d9428a427a6c5434c8efbadf4b71d

open "Build/unreal-shipping/archive/Mac/VirtualRowing-Mac-Shipping.app" \
  --args -windowed -ResX=1440 -ResY=900 \
  -ContentCatalogUrl="https://localhost/vir/${PB_PREFIX}.pb"

