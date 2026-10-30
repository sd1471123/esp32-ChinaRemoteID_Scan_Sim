# 网页 HTTPS 用的自签证书

浏览器只在**安全上下文**（`https://` 或 `localhost`）里暴露 `navigator.bluetooth`，
而设备热点地址 `http://192.168.4.1` 属于不安全来源，网页蓝牙会被直接隐藏。
所以设备额外在 443 端口起一个 HTTPS 副本，用这两个文件作为服务端证书。

- `servercert.pem` / `prvtkey.pem`：ECDSA P-256 自签，`CN=192.168.4.1`，
  SAN 含 `192.168.4.1`、`127.0.0.1`、`esp32crid.local`，有效期到 2046-10-25。
- 两个文件在编译时由 `main/CMakeLists.txt` 的 `EMBED_TXTFILES` 打进固件，
  运行时通过 `_binary_servercert_pem_start` / `_binary_prvtkey_pem_start` 引用。
- **这不是秘密**：所有设备共用同一份自签证书，私钥就放在仓库里。它只用于让浏览器
  把页面视作安全上下文，不承担身份认证——设备真正的访问控制是热点 WPA2 口令。
  浏览器首次访问会提示证书不受信任，点「继续访问」即可。

重新生成（会把证书换成新的，浏览器需要重新点一次「继续访问」）：

```bash
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 \
  -keyout prvtkey.pem -out servercert.pem -days 7300 -nodes \
  -subj "/C=CN/O=ESP32-CRID/CN=192.168.4.1" \
  -addext "subjectAltName=IP:192.168.4.1,IP:127.0.0.1,DNS:esp32crid.local"
```
