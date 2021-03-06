#define NOMINMAX
#define LOG_REQUEST 0
#define DUMP_REQUEST 0

/*
Todo

1. Add Timeout 10 seconds.
2. Add sql injection part.
3. Trim Leak Vulnerability Scenario
4. Check If This binary is exploitable.
5. ASLR ON
*/

#include "route.h"
#include "util.h"

#include <algorithm>


int connection(SOCKET cli_socket)
{

	char buffer[BUFSIZE + 1];
	long idx, nbytes;
	char * pos;
	int max_req = 10;
	int i = 0;
	int r_idx = 0;
	char dest[1024] = {};

	struct sockaddr_in addr;
	int addrlen = sizeof(addr);
	char addrstr[30];

	memset(buffer, 0, sizeof(buffer));
	nbytes = recv(cli_socket, (char *)buffer, BUFSIZE, 0);
	// puts((const char *)buffer);
	if (nbytes == 0 || nbytes == -1)
	{
		puts("failed to read browser request");
		return -1;
	}

	getpeername(cli_socket, (struct sockaddr *)&addr, &addrlen);
	sprintf_s(addrstr, "%hhd.%hhd.%hhd.%hhd (%dB)",
		addr.sin_addr.S_un.S_un_b.s_b1,
		addr.sin_addr.S_un.S_un_b.s_b2,
		addr.sin_addr.S_un.S_un_b.s_b3,
		addr.sin_addr.S_un.S_un_b.s_b4,
		nbytes
	);

#if DUMP_REQUEST
	FILE *fp = NULL;
	fopen_s(&fp, "C:\\Users\\berry\\output.bin", "ab+");
	if (fp) {
		fwrite(buffer, 1024, 1, fp);
		fclose(fp);
	}
#endif

	buffer[nbytes] = 0;
	pos = buffer;

	int offset = 0;
	for (idx = 0; idx < nbytes - offset; idx++) {
		if (pos[idx] == ' ') {
			pos += idx + 1;
			break;
		}
	}
	offset = pos - buffer;
	size_t url_len = 0;
	for (idx = 0; idx < nbytes - offset; idx++)
	{
		if (pos[idx] == '?')
		{
			url_len = idx;
		}
		if (pos[idx] == ' ')
		{
			if(!url_len)
				url_len = idx;
			pos[idx] = 0;
			break;
		}
	}

	for (auto *route : *getRoutes()) {
		if (!strncmp(pos, route->path(), std::max(url_len, strlen(route->path()))) &&
			!strncmp(buffer, route->method(), strlen(route->method()))) {
			char *query = (char *)"";
#if LOG_REQUEST
			printf("%s - \"%s %s\" -> %s\n", addrstr, route->method(), pos, route->path());
#endif
			int len = 0;
			//if (!strcmp(route->method(), "GET")) {
				/* /url?query=value */
				char *query_ptr = pos + strlen(route->path());
				if (*query_ptr == '?') {
					query_ptr++;
					const char *query_end = strchr(query_ptr, ' ');
					if (!query_end) {
						query_end = buffer + nbytes;
					}
					if (query_end != NULL)
					{
						query = query_ptr;
						len = query_end - query_ptr;
					}
				}
			//}
			/*else {
				// HEADER \r\n\r\n query=value
				char *query_ptr = strstr(pos, "\r\n\r\n");
				if (!query_ptr)
					query_ptr = strstr(pos, "\n\n");
				if (query_ptr) {
					query = query_ptr;
					len = nbytes - (query_ptr - buffer);
				}
			}*/
			route->handler()(cli_socket, query, len);
			return 0;
		}
	}
	
#if LOG_REQUEST
	buffer[url_len] = 0;
	printf("%s - \"%s\" (%d, %d) => 404\n", addrstr, buffer, offset, url_len);
#endif
	_notFound(cli_socket);
	return 0;
}


int keepalive(SOCKET socket) {
	
	for(int i = 0; i < 100; i++)
		if(connection(socket)) break;
	
	closesocket(socket);
	
	return 0;
}


int authorizer(void*unused, int action, const char*, const char*, const char*, const char*) {
	if (action == SQLITE_ATTACH || action == SQLITE_DETACH)
		return SQLITE_DENY;
	return SQLITE_OK;
}


void db_init(void)
{
	int iresult;
	char * zErrMsg = 0;
	iresult = sqlite3_open(":memory:", &db);
	if (iresult)
	{
		puts("Can't open database");
		exit(-1);
	}

	sqlite3_enable_load_extension(db, 0);
	sqlite3_set_authorizer(db, authorizer, NULL);

	iresult = sqlite3_exec(db, create_table, NULL, 0, &zErrMsg);
	if (iresult != SQLITE_OK)
	{
		puts("sqlite: CREATE TABLE ERROR");
		puts(zErrMsg);
		exit(-1);
	}

}

#define DEFAULT_PORT "12345"

int main(int argc, char ** argv)
{

	WSADATA wsaData;
	SOCKET listenfd, socketfd;
	static struct sockaddr_in cli_addr;
	static struct sockaddr_in serv_addr;
	unsigned short port;
	int length;
	int req;
	int iresult;
	struct addrinfo * result = NULL;
	struct addrinfo hints;

	setvbuf(stdout, (char *)NULL, _IONBF, 0);

	/* printf("sqlite3 Version : %s\n\n\n", sqlite3_libversion()); */
	db_init();

	/*
	puts("============================");
	puts(" Starting ...  ");
	puts("============================\n");
	*/

	iresult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iresult != NO_ERROR)
	{
		puts("WSAStartup Error");
		exit(-1);
	}

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	if (argc >= 2)
	{
		port = (unsigned short)atoi(argv[1]);
	}
	else
	{
		port = 0;
	}

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(0x7f000001);
	serv_addr.sin_port = htons(port);

	listenfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
	{
		printf("Error Code : %d\n", WSAGetLastError());
		printf("ERROR: Can't bind a socket to port %d, another \n", port);
		exit(-1);
	}

	int len = sizeof(struct sockaddr);
	if (getsockname(listenfd, (struct sockaddr *)&serv_addr, &len) == -1)
	{
		puts("getsockname Error!");
		exit(-1);
	}

	printf("PORT : %d\n", ntohs(serv_addr.sin_port));
	if (listen(listenfd, 1) < 0)
	{
		puts("ERROR: listen failed");
		exit(-1);
	}

	for ( ;; )
	{
		length = sizeof(cli_addr);
		socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length);
		if (socketfd<0)
		{
			puts("ERROR: accept failed");
			exit(-1);
		}
		CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)keepalive, (LPVOID)socketfd, NULL, NULL);
		/*
		connection(socketfd);
		shutdown(socketfd, SD_BOTH);
		*/
	}

}