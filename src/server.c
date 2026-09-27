#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>

/*
HTTPサーバーをつくるというよりはTCPサーバーを作るという方が近い。
利用するsystem call
- socket()
- bind()
- listen()
- accept()

manの代表的なセクション番号について
1. コマンド
2. system call
3. Cライブラリ関数
5. ファイル形式
7. 規約・概念

socketのmanualセクション番号2を見たければ
man socket 2
とする

includeしているsys/socket.hは/usr/include/sys/socket.hあたりにある。
MacOSだとそもそも/usr/includeが存在せず、XcodeのSDK内にある。
$ fd --search-path $(xcrun --show-sdk-path) socket.h
みたいな感じで確認できた。

socket(int domain, int type, int protocol)
  domain: man socket 2に書いてある定数でok。(#include
<sys/socket.h>で読み込んでいる) type: man socket 2に書いてある定数でok。
  protocol:
    see protocols(5).とある。man protocols
5で見てみると具体的な定義は書いていなくてFILESにファイルパスが書いてある。(/etc/protocols)
    150種類くらい定義されている。

HTTPサーバーを実装するに当たり、どのプロトコルを使えば良い？
HTTPのRFCを読むと特定のポートでTCP接続を待ち受けるとあるので、TCPを使えば良さそう。
https://www.rfc-editor.org/info/rfc9110#section-4.2.1
ただし、HTTPはトランスポートプロトコルと独立していると記述してあるので、HTTPだからトランスポートプロトコルはTCP固定ではない。
https://www.rfc-editor.org/info/rfc9110#name-http-origins


*/
int main(int argc, const char *argv[]) {
  int socket_fd;
  int bind_fd;

  socket_fd = socket(AF_INET, SOCK_STREAM, 6);
  // man bind 2 のexampleのエラー処理を参考にした
  if (socket_fd == -1) {
    err(EXIT_FAILURE, "socket");
  }
  printf("create socket.\n");

  return 0;
}
