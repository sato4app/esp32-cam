#pragma once

// スマホ向けのWebサーバ
//   ポート80: 画面（index.html）・操作API・microSDのファイル
//   ポート81: ライブ映像（MJPEG）。映像の送信中も操作APIが止まらないよう別サーバにしている
namespace AppServer {

void begin();

} // namespace AppServer
