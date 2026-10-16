
/*
 * TCPサーバーを作ってHTTPをやり取りするという考え方のほうが正確そう。
 * 利用するsystem call
 * - socket()
 * - bind()
 * - listen()
 * - accept()
 *
 * manの代表的なセクション番号について
 * 1. コマンド
 * 2. system call
 * 3. Cライブラリ関数
 * 5. ファイル形式
 * 7. 規約・概念
 *
 * socketのmanualセクション番号2を見たければ
 * man 2 socket
 * とする
 *
 * includeしているsys/socket.hは/usr/include/sys/socket.hあたりにある。
 * MacOSだとそもそも/usr/includeが存在せず、XcodeのSDK内にある。
 * $ fd --search-path $(xcrun --show-sdk-path) socket.h
 * みたいな感じで確認できた。
 *
 * socket(int domain, int type, int protocol)
 *   domain: man 2 socket に書いてある定数でok。(#include
 * <sys/socket.h>で読み込んでいる) type: man 2 socket に書いてある定数でok。
 *   protocol:
 *     see protocols(5).とある。man 5 protocols
 * で見てみると具体的な定義は書いていなくてFILESにファイルパスが書いてある。(/etc/protocols)
 *     150種類くらい定義されている。
 *
 * HTTPサーバーを実装するに当たり、どのプロトコルを使えば良い？
 * HTTPのRFCを読むと特定のポートでTCP接続を待ち受けるとあるので、TCPを使えば良さそう。
 * https://www.rfc-editor.org/info/rfc9110#section-4.2.1
 * ただし、HTTPはトランスポートプロトコルと独立していると記述してあるので、HTTPだからトランスポートプロトコルはTCP固定ではない。
 * https://www.rfc-editor.org/info/rfc9110#name-http-origins
 *
 * bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen):
 *   sockfd: socketシステムコールの戻り値であるfile descriptorを渡せば良い
 *   *addr: C言語の言語仕様を調べる必要があるが、構造体のポインタを渡せばよい？
 *   addrlen: addrから計算できそうだが引数で渡す必要があるのか？
 *
 * そもそもman bindのようにセクション番号指定する必要ない？
 * どのセクションを見ているかを意識しておけば問題なさそう。
 * 他にセクションがあるのかというのは man -f socket とすると確認できる。
 *
 * HTTP Responseを作ってクライアントに返す必要がある。
 * man -k httpで調べたらpm3(Perl Module Section 3)がヒットした。
 * LLMによると歴史的な経緯からPerl Moduleはmanualに含まれているとのこと。
 * Rustの場合はrustup doc --std
 * でオフライン環境でも標準ライブラリのドキュメントが読める。
 * 脱線したが、HTTPのRFCを読んで仕様通りのHTTP Responseを作る。
 * https://www.rfc-editor.org/rfc/rfc9112.html
 * obsoleteとなっていれば最新の仕様ではないので注意する。
 * C言語では文字列は文字の配列として表され、\0が文字列終端を表す。
 *
 */

#include <arpa/inet.h> // man -k ipv4 -> man 3 inet_pton
#include <asm-generic/socket.h>
#include <ctype.h>
#include <err.h>
#include <netinet/in.h> // man sockaddr
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define LISTEN_BACKLOG 50 // man bind(2)のexampleを参考にそのまま流用
// man bind(2)のexampleを参考にそのまま流用
#define handle_error(msg)                                                      \
  do {                                                                         \
    perror(msg);                                                               \
    exit(EXIT_FAILURE);                                                        \
  } while (0)

typedef struct {
  const char *receved_buf;
  size_t method_start, method_size;
  size_t request_target_start, request_target_size;
} http_message;

typedef struct {
  const char *receved_buf;
  size_t path_start, path_size;
  size_t query_start, query_size;
} request_line;

typedef struct {
  const char *receved_buf;
  size_t key_start, key_size;
  size_t value_start, value_size;
} query;

int assert_http_method(const http_message *http_message, const char *method) {
  if (http_message->method_size != strlen(method)) {
    printf("method_size: %ld != method_strlen: %ld\n",
           http_message->method_size, strlen(method));
    return -1;
  }
  return memcmp(http_message->receved_buf + http_message->method_start, method,
                http_message->method_size);
}

int parse_http_message(const char *buffer, unsigned long buffer_len,
                       http_message *out) {
  /*
   * https://www.rfc-editor.org/rfc/rfc9112.html#name-message-format
   * start-lineを見つける。最初のCRLFを探す。
   */
  size_t start_line_start = 0, start_line_size = 0;
  // buf_len - 1 で終端文字分を確保しとく。
  for (int i = 0; i < buffer_len - 1; i++) {
    if (buffer[i] == '\n' && 0 < i && buffer[i - 1] == '\r') {
      start_line_size = i;
      break;
    }
  }
  if (start_line_size == 0) {
    printf("start_line parse error.\n buffer: %s", buffer);
    return -1;
  }

  printf("start_line: ");
  for (int i = start_line_start; i < start_line_size; i++) {
    printf("%c", buffer[i]);
  }
  printf("\n"); // GET /calc?q=1%2B2 HTTP/1.1

  /*
   * https://www.rfc-editor.org/rfc/rfc9112.html#name-request-line
   * start-lineのうちrequest-lineからmethodとrequest-targetを取り出してhttp_message構造体を作る。
   * LLMに聞いたところ、SP（空白）の位置を特定して先頭位置からSPまでの長さという感じでポインタで管理すると良いらしい。
   * ctype.hに標準ブランク文字を判定する関数isblankがある。
   * https://ja.wikibooks.org/wiki/C%E8%A8%80%E8%AA%9E/%E6%A8%99%E6%BA%96%E3%83%A9%E3%82%A4%E3%83%96%E3%83%A9%E3%83%AA/ctype.h#isblank%E9%96%A2%E6%95%B0
   */
  out->receved_buf = buffer;
  out->method_start = 0;
  out->method_size = 0;
  out->request_target_start = 0;
  out->request_target_size = 0;
  for (int i = start_line_start; i < start_line_size; i++) {
    if (isblank(buffer[i])) {
      if (out->request_target_start != 0 && out->request_target_size == 0) {
        out->request_target_size = i - out->request_target_start;
      }
      if (out->method_size == 0) {
        out->method_size = i;
        out->request_target_start = i + 1;
      }
    }
  }

  printf("method: ");
  /*
   * out->method + out->method_size について。
   * out->method_startは位置0なので加算する必要はないものの、オフセットを見るという観点で足している。
   */
  for (int i = out->method_start; i < out->method_start + out->method_size;
       i++) {
    printf("%c", buffer[i]);
  }
  printf("\n");

  printf("request-target: ");
  for (int i = out->request_target_start;
       i < out->request_target_start + out->request_target_size; i++) {
    printf("%c", buffer[i]);
  }
  printf("\n");

  return 0;
}

int parse_request_line(const http_message *http_message, request_line *out) {
  out->receved_buf = http_message->receved_buf;
  out->path_start = 0, out->path_size = 0, out->query_start = 0,
  out->query_size = 0;

  const char *pos =
      memchr(http_message->receved_buf + http_message->request_target_start,
             '?', sizeof(http_message->request_target_size));
  if (pos == NULL) {
    handle_error("parse request line error. query(?) not found.");
    return -1;
  }
  // GET /calc?q=1%2B2 HTTP/1.1
  // '?'自体はpathとqueryの区切り文字なので、auery文字列に含めないので+1
  out->query_start = pos - http_message->receved_buf + 1;
  out->path_start = http_message->request_target_start;
  // -1で'?'分を調整
  out->path_size = out->query_start - http_message->request_target_start - 1;
  out->query_size = http_message->request_target_start +
                    http_message->request_target_size - out->query_start;
  printf("path start: %ld\n", out->path_start);
  printf("path size: %ld\n", out->path_size);
  printf("query start: %ld\n", out->query_start);
  printf("query size: %ld\n", out->query_size);

  return 0;
}

int parse_query(const request_line *request_line, query *out) {
  out->receved_buf = request_line->receved_buf;
  out->key_start = 0, out->key_size = 0, out->value_start = 0,
  out->value_size = 0;

  const char *pos = memchr(request_line->receved_buf + request_line->path_start,
                           '=', sizeof(request_line->path_size));
  if (pos == NULL) {
    handle_error("query string error. '=' not found.");
    return -1;
  }
  // GET /calc?q=1%2B2 HTTP/1.1
  size_t equal_token_position = pos - out->receved_buf;
  out->key_start = request_line->query_start;
  out->key_size = equal_token_position - request_line->query_start;
  out->value_start = equal_token_position + 1;
  out->value_size =
      request_line->query_start + request_line->query_size - out->value_start;
  printf("= position: %ld\n", pos - out->receved_buf);
  printf("query key start: %ld\n", out->key_start);
  printf("query key size: %ld\n", out->key_size);
  printf("query value start: %ld\n", out->value_start);
  printf("query value size: %ld\n", out->value_size);

  return 0;
}

int hex_to_int(char c) {
  if (isdigit(c)) {
    return c - '0';
  }
  if (islower(c)) {
    return c - 'a' + 10;
  }
  if (isupper(c)) {
    return c - 'A' + 10;
  }

  return -1;
}

int url_percent_decode(const query *query, char *out) {
  int i = query->value_start;
  int j = 0;
  int query_value_len = query->value_start + query->value_size;
  for (; i < query_value_len; i++, j++) {
    printf("i: %d\n", i);
    printf("receved_buf[i]: %c\n", query->receved_buf[i]);
    if (query->receved_buf[i] != '%') {
      out[j] = query->receved_buf[i];
      printf("out[j]: %c\n", out[j]);
      continue;
    }
    // percent decode
    // %を見つけたら2文字先読みできそうか。
    if (query_value_len <= i + 2) {
      handle_error("invalid query string. checked length");
      break;
    }

    char first_c = query->receved_buf[i + 1];
    char second_c = query->receved_buf[i + 2];
    if (isxdigit(first_c) == 0 || isxdigit(second_c) == 0) {
      printf("first_c: %c\n", first_c);
      printf("second_c: %c\n", second_c);
      handle_error("invalid query string");
      break;
    }

    int first_c_int = hex_to_int(first_c);
    int second_c_int = hex_to_int(second_c);
    printf("first_c_int: %d\n", first_c_int);
    printf("second_c_int: %d\n", second_c_int);
    if (first_c_int == -1 || second_c_int == -1) {
      handle_error("failed hex to int");
      break;
    }
    char decoded_c = (first_c_int << 4) + second_c_int;
    printf("decoded_c: %c\n", decoded_c);
    out[j] = decoded_c;
    printf("out[j]: %c\n", out[j]);
    i = i + 2;
  }

  out[j + 1] = '\0';
  return 0;
}

int main(int argc, const char *argv[]) {
  /* ========== SOCKET ========== */
  int socket_fd;
  socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  // man 2 bind のexampleのエラー処理を参考にした
  if (socket_fd == -1) {
    handle_error("socket");
  }
  printf("create socket ok.\n");
  /*
   * Address alredy in use対応
   * socketoptで再利用するオプションを設定する。（LLMに聞いた）
   */
  int opt = 1;
  if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) ==
      -1) {
    handle_error("set socket opt");
  }
  /* ========== SOCKET ========== */

  /* ========== BIND ========== */
  int bind_fd;
  int ipv4_addr;
  struct in_addr in_addr_t;
  struct sockaddr_in sockaddr_in_t;
  if (inet_pton(AF_INET, "127.0.0.1", &in_addr_t) == -1) {
    handle_error("inet_pton");
  }
  sockaddr_in_t.sin_family = AF_INET;
  sockaddr_in_t.sin_addr = in_addr_t;
  /*
   * Linuxでは1024以下のポート番号はprivileged port
   * なので、一般ユーザーのプロセスではbind()できない。
   * 80はprivileged portなので8080にしておく。
   */
  sockaddr_in_t.sin_port = htons(8081);
  // bindの第2引数はsockaddrのポインタを取るので、ipv4を表すsockaddr_inを渡したければsockaddr_inから
  // sockaddrのポインタとしてキャストする必要がある。
  // https://stackoverflow.com/questions/51757117/cast-struct-pointer-to-another-struct
  bind_fd = bind(socket_fd, (const struct sockaddr *)&sockaddr_in_t,
                 sizeof(sockaddr_in_t));
  if (bind_fd == -1) {
    handle_error("bind");
  }
  printf("socket_fd bind ok.\n");
  /* ========== BIND ========== */

  /* ========== LISTEN ========== */
  int listen_fd = listen(socket_fd, LISTEN_BACKLOG);
  if (listen_fd == -1) {
    handle_error("listen");
  }
  printf("listen ok.\n");
  /* ========== LISTEN ========== */

  /* ========== ACCEPT ========== */
  socklen_t addr_size;
  addr_size = sizeof(sockaddr_in_t);
  int accept_fd =
      accept(socket_fd, (struct sockaddr *)&sockaddr_in_t, &addr_size);
  if (accept_fd == -1) {
    handle_error("accept");
  }
  printf("accept ok.\n");
  /* ========== ACCEPT ========== */

  /* ========== RECV ========== */
  char recived_buffer[1000];
  int recived_len = recv(accept_fd, &recived_buffer, sizeof(recived_buffer), 0);
  if (recived_len == -1) {
    handle_error("recv");
  }
  printf("recived data len:%d\n", recived_len);
  printf("recived data:\n%s\n", recived_buffer);
  /* ========== RECV ========== */

  /* ========== HTTP Request Parse ========== */
  http_message http_message;
  if (parse_http_message(recived_buffer, sizeof(recived_buffer),
                         &http_message) == -1) {
    handle_error("parse http request");
  }
  // HTTP MethodがGETでなければエラーとする。
  char alowed_method[] = "GET";
  if (assert_http_method(&http_message, alowed_method) != 0) {
    handle_error("http method must be GET");
  }
  request_line request_line;
  parse_request_line(&http_message, &request_line);
  query query;
  parse_query(&request_line, &query);
  char decoded_query_value[query.value_size];
  url_percent_decode(&query, decoded_query_value);
  printf("decoded_query_len: %ld\n", strlen(decoded_query_value));
  printf("decoded_query_value: %s\n", decoded_query_value);
  /* ========== HTTP Request Parse ========== */

  /* ========== CalcuLate ========== */
  /* ========== CalcuLate ========== */

  /* ========== HTTP Response ========== */

  /* ========== HTTP Response ========== */

  if (close(socket_fd) == -1) {
    handle_error("close");
  }
  printf("socket closed.\n");
  return 0;
}
