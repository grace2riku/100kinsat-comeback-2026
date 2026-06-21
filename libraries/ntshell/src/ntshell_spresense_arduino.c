#include "ntshell.h"
#include "ntopt.h"
#include "ntlibc.h"

#define INITCODE (0x4367)
#define SERIAL_READ(HANDLE, BUF, CNT) ((HANDLE->func_read(BUF, CNT, (HANDLE)->extobj)))

void ntshell_execute_spresense_arduino(ntshell_t* p) {
  if (p->initcode != INITCODE) {
    return;
  }

  /*
   * func_read が 0 を返す場合（例: CRLF の LF を読み飛ばしたとき）に
   * 未初期化の ch をパーサへ渡さないよう、読めたバイト数を確認する。
   */
  unsigned char ch;
  int n = SERIAL_READ(p, (char*)&ch, sizeof(ch));
  if (n <= 0) {
    return;
  }
  vtrecv_execute(&(p->vtrecv), &ch, sizeof(ch));
}