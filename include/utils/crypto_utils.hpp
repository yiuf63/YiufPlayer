#pragma once

#include <string>

namespace muvik {
namespace crypto {

// 计算 MD5 摘要，返回 32 位小写十六进制字符串
std::string md5(const std::string& input);

// 计算 SHA-256 摘要，返回 64 位小写十六进制字符串
std::string sha256(const std::string& input);

// 计算 HMAC-SHA256 签名，返回 64 位小写十六进制字符串
std::string hmacSha256(const std::string& key, const std::string& data);

// Base64 编码
std::string base64Encode(const std::string& input);

// Base64 解码（失败返回空字符串）
std::string base64Decode(const std::string& input);

} // namespace crypto
} // namespace muvik
