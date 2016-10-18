# tinyip
TOPPERS/ASP Kernelを載せたGR-PEACH（マイコンボード）上で動くTCP/IPプロトコルスタックです。

ネットワークの勉強がてら遊びで作っているので、手抜きなところやまずい部分が多々あると思われます..
そのため実用には向きませんのでご注意ください。
なお、下のリンクのTOPPERS/ASP KernelにはlwipというTCP/IPプロトコルスタックが付属しています。ですから、要するに車輪の再発明です。

プロトコルスタックの実装についてちゃんと勉強してやってるわけではないので、あんまり参考にしないほうがいいと思います（「じゃあ公開する意味はあるのか」と言われそうですが...）。

# 実装したもの
* ARP
  - リクエストに対する応答
  - リクエスト送信、アドレス解決ができるまでIPパケット送信の保留
* IP
  - フラグメント分割と組み立て（タイムアウト付き）
  - 宛先が同一ネットワークでなければデフォルトゲートウェイへ転送
* ICMP
  - エコー要求に対する応答のみ
* UDP
  - 受信したUDPデータグラムのキューイング
  - ソケット風API
* SNTPクライアント
  - UDP通信のテストとして書きました
* TCP
  - 再送
  - スライディング・ウィンドウによるフロー制御
  - 遅延ACK
  - ソケット風API
* Webサーバ
  - GETを受けたらデータを送るだけの、最低限のもの
* ネームリゾルバ
* DHCPクライアント
* Twitter APIを叩いてツイートするプログラム

Ethernetコントローラの制御には、mbedのEthernetコントローラドライバを利用しています。

Twitter APIの利用に必要な、TLS通信・HMAC-SHA-1・Base64エンコードにwolfSSL(SSL/TLSライブラリ, http://www.wolfssl.jp/wolfsite/ )を利用しました。

# コンパイル
以下のGR-PEACH版TOPPERS/ASP Kernel(mbed/Arduinoライブラリ付属)をダウンロードします。

https://github.com/ncesnagoya/asp-gr_peach_gcc-mbed

exampleディレクトリがあるので、その中にtinyipを置きます。
memo.txtにしたがって、ASPカーネルのMakefileとヘッダファイルの修正を行ってください。
tweet.cppではwolfSSLを使用しているためwolfSSLのコンパイルも必要です（wolfssl-build.txt参照）。
twitter_keys.hで宣言されているTwitterコンシューマキー・アクセストークンをどこかで定義してやる必要があります。

~~~
make depend; make
~~~
でコンパイルできます。正常にコンパイルができたらasp.binが生成されるので、これをマイコンに転送してください。

# 参考書籍・Webページ
* RFC(1122,791,792,793,826,768,815,2131,2132あたり)
* 基礎から分かるTCP/IPネットワーク実験プログラミング(村山公保,オーム社)
* 詳解TCP/IP Vol.1 プロトコル(W・リチャード・スティーヴンス,ピアソン・エデュケーション)
* コンピュータネットワーク 第5版(アンドリュー・S・タネンバウム,デイビッド・J・ウエザロール,日経BP)
* Interface連載 パケットづくりではじめるネットワーク入門(坂井弘亮,CQ出版社)
* 本当の基礎からのWebアプリケーション入門---Webサーバを作ってみよう(http://kmaebashi.com/programmer/webserver/)
* TCP詳説(西田佳史,https://www.nic.ad.jp/ja/materials/iw/1999/notes/C3.PDF)
* TOPPERS新世代カーネル統合仕様書(https://mitsut.github.io/toppers_kernel_spec/)
* DNSクライアントを作ってみよう(http://x68000.q-e-d.net/~68user/net/resolver-1.html)
* @IT ネットワーク・コマンドでトラブル解決（6）：DHCP設定は正しいか？～DHCP設定の確認と利用～(http://www.atmarkit.co.jp/ait/articles/0202/26/news001.html)
* @IT DNS Tips：DNSパケットフォーマットと、DNSパケットの作り方(http://www.atmarkit.co.jp/ait/articles/1601/29/news014.html)
* Twitter Developers(https://dev.twitter.com/)
* Syncer Twitter APIの使い方まとめ(https://syncer.jp/twitter-api-matome)
* wolfSSL ドキュメント(http://www.wolfssl.jp/wolfsite/documents/)

# License
* Unlicense(Public Domain)
