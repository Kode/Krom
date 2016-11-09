#ifdef SYS_OSX

#include <CommonCrypto/CommonDigest.h>

#include <string>

std::string sha1(const char* data, int length) {
	CC_SHA1_CTX sha;
	CC_SHA1_Init(&sha);
	CC_SHA1_Update(&sha, data, length);
	char result[256];
	CC_SHA1_Final((unsigned char*)result, &sha);
	return result;
}

#endif
