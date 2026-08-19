# NOTICE

`LICENSE`(Apache License 2.0)は、本リポジトリ内でオリジナルに作成した部分
(`CPU0/src/`・`CPU1/src/`、および`doc/`内のドキュメント)に適用される。

以下のベンダー/サードパーティ由来のコードは、それぞれの元のライセンス条件に従う。
本リポジトリへの同梱は、ビルドをそのまま再現できるようにするためのものである。

- `CPU0/ra/`, `CPU1/ra/`: Renesas Flexible Software Package(FSP)。Renesas Electronics
  Corporationの提供するライセンス条件に従う(`CPU0/ra/arm/CMSIS_6/LICENSE`等、
  各サブフォルダ内のライセンス表記を参照)。
- `CPU0/mtk3_bsp2/`, `CPU1/mtk3_bsp2/`: μT-Kernel 3.0 BSP(TRONフォーラム提供)。
  T-License 2.x に従う。
- `CPU0/ra_gen/`, `CPU1/ra_gen/`, `CPU0/ra_cfg/`, `CPU1/ra_cfg/`: 上記FSPの
  コード生成ツールによる自動生成物。
