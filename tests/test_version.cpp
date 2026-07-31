// version() must report the real library version.
//
// It used to return the literal "rudeserver.com SMTP version 1.0": frozen
// while configure.ac moved to 1.0.0 and beyond, and naming a host that no
// longer resolves. A caller had no way to find out what they were linked
// against, which is the one thing the call exists for.
#include <rude/smtp.h>

#include <cstdio>
#include <cstring>

int main()
{
	const char *v = rude::SMTP::version();

	if(!v)
	{
		std::fprintf(stderr, "FAIL: version() returned null\n");
		return 1;
	}
	if(std::strcmp(v, RUDESMTP_EXPECTED_VERSION) != 0)
	{
		std::fprintf(stderr, "FAIL: version() is \"%s\", expected \"%s\"\n",
					 v, RUDESMTP_EXPECTED_VERSION);
		return 1;
	}
	if(std::strstr(v, "rudeserver.com") != 0)
	{
		std::fprintf(stderr, "FAIL: version() still names a dead host: \"%s\"\n", v);
		return 1;
	}

	std::printf("version() reports %s\n", v);
	return 0;
}
