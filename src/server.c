
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
#include <err.h>
#include <netinet/in.h> // man sockaddr
#include <stdio.h>
#include <stdlib.h>
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
  char method[3];         // 暫定で GET だけ対応する
  char request_target[5]; // 暫定で 1%2B2 だけ対応する
} http_message;

int parse_http_message(const char *buf, unsigned long buf_len,
                       http_message *http_message);

int main(int argc, const char *argv[]) {
  /* ========== SOCKET ========== */
  int socket_fd;
  socket_fd = socket(AF_INET, SOCK_STREAM, 6);
  // man 2 bind のexampleのエラー処理を参考にした
  if (socket_fd == -1) {
    handle_error("socket");
  }
  printf("create socket ok.\n");
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
  sockaddr_in_t.sin_port = htons(8080);
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
  /* ========== HTTP Request Parse ========== */

  /* ========== HTTP Response ========== */
  /* ========== HTTP Response ========== */

  if (close(socket_fd) == -1) {
    handle_error("close");
  }
  return 0;
}

int parse_http_message(const char *buf, unsigned long buf_len,
                       http_message *http_message) {
  /*
   * https://www.rfc-editor.org/rfc/rfc9112.html#name-message-format
   * start-lineを見つける。最初のCRLFを探す。
   */
  char start_line_buf[buf_len];
  for (int i = 0; i < buf_len; i++) {
    if (buf[i] == '\n' && 0 < i && buf[i - 1] == '\r') {
      break;
    }
    start_line_buf[i] = buf[i];
  }
  printf("start-line: %s\n", start_line_buf);
  return 0;
}
