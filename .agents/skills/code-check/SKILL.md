---
name: code-check
description: >-
  LiteProcManager（C++17 / Win32 GUI アプリケーション）のセキュリティ脆弱性、
  メモリ・カーネルハンドル・GDIリソースリーク、並行処理やオブジェクト寿命のバグ、
  および Win32 API のロジック不具合を監査・チェックします。
  ユーザーからプロジェクト全体または特定の変更に対して脆弱性、バグ、メモリリーク、
  ハンドルリーク、回帰（リグレッション）のチェック・監査・レビューを求められた際に使用します。
---

# LiteProcManager 脆弱性・バグ・メモリリーク監査スキル (`code-check`)

このスキルは、**LiteProcManager**（`src/`、`include/`、`tests/`、`installer/`、およびビルド設定）におけるセキュリティ脆弱性、メモリ／ハンドル／GDIリーク、機能バグを体系的に検査するための手順を提供します。

---

## 監査ワークフロー

### ステップ 1: 自動ヒューリスティックスキャンとテストの実行
同梱の PowerShell 補助スクリプトを実行し、リソース確保・解放 API の呼び出しバランス、高リスクな Win32 API 呼び出し箇所、バイナリのセキュリティ防御フラグ（`dumpbin`）、および単体テスト結果（`vstest.console.exe`）を収集します。

```powershell
powershell -ExecutionPolicy Bypass -File .\.agents\skills\code-check\scripts\run_audit_checks.ps1
```
*（オプション引数: ビルド済みバイナリが最新の場合は `-SkipBuild`、静的スキャンのみを素早く行う場合は `-SkipTests` を指定可能）*

> [!NOTE]
> シェル環境変数に `PATH` と `Path` が重複して存在する場合、`MSBuild` が `MSB6001` エラーを返すことがあります。その場合は環境変数を正規化するか、`-SkipBuild` を付けて既存バイナリとソースコードを検査してください。

### ステップ 2: 既存の調査レポート（ベースライン）の確認
リポジトリ直下の [survey.md](../../../survey.md) を読み込み、過去に検出された課題（`SEC-*`、`PERF-*`、`IMP-*`）のうち、どれが修正済みで、どれが未対応または一部修正であるかを把握します。また、直近のコード変更によって修正済み項目が回帰（リグレッション）していないかを確認します。

### ステップ 3: 詳細チェックリストに基づく深掘りコード監査
[references/checklist.md](./references/checklist.md) を読み込み、以下の3つの主要観点に沿ってソースコードを網羅的に監査します。

1. **セキュリティ・脆弱性 (`SEC`)**
   - **PID 再利用 / TOCTOU**: プロセス終了、優先度変更、プロセスツリー終了、CPU 使用率の差分計算において、`GetProcessTimes` で `(process_id, creation_time)` の一致を検証しているか、また親子ツリーの循環参照を遮断しているか（`src/main_window.cc`, `src/process_snapshot_service.cc`）。
   - **管理者昇格とサブプロセス起動ハイジャック**: 昇格実行中に相対パスやファイル名のみで `ShellExecuteW` / `CreateProcessW` を呼び出していないか。「ファイルの場所を開く」で `SHOpenFolderAndSelectItems`（PIDL）を使用しているか。URL 検索文字列が RFC 3986 でパーセントエンコードされているか（`include/url_helper.h`）。
   - **単一起動（Single-Instance）と IPC**: 名前付き Mutex の先取り（Squatting）対策、昇格再起動時のウィンドウクラス名なりすまし防止（`src/main.cc`, `src/main_window.cc`）、および `WM_APP` カスタムメッセージのポインタ所有権が安全か。
   - **外部入力とエクスポートの無害化**: TSV／クリップボード出力時の数式インジェクション対策（`include/tsv_helper.h`, `src/process_export.cc`）、設定 JSON のサイズ・ネスト深さ上限とスキーマ境界値検証（`include/json_helper.h`, `src/app_settings.cc`）、レジストリ値の型とパスの検証。
   - **バイナリ防御設定**: `.vcxproj` と `CMakeLists.txt` の双方で `/guard:cf`、`/CETCOMPAT`、`/Qspectre`、`/sdl`、`/W4`、ASLR、NX が適切に設定されているか。

2. **メモリ・カーネルハンドル・GDI リーク (`LEAK` / `PERF`)**
   - **ウィンドウとヒープの寿命管理**: 自己解放型ウィンドウ（`src/toast_window.cc` 等）が `WM_DESTROY` ではなく `WM_NCDESTROY` で `GWLP_USERDATA` をクリアし `DefWindowProcW` 完了後に `delete this` しているか。また、`CreateWindowExW` 失敗時にオブジェクトと GDI リソースがリークしないか。
   - **カーネル・SCM ハンドル**: `OpenProcess`、`OpenProcessToken`、`CreateToolhelp32Snapshot`、`OpenSCManagerW`、`OpenServiceW`、`RegOpenKeyExW`、`ILCreateFromPathW` について、早期リターンやエラー分岐を含むすべての経路で対応する解放関数（`CloseHandle`, `CloseServiceHandle`, `RegCloseKey`, `ILFree`）が呼ばれているか。
   - **GDI・USER オブジェクト**: `CreateFontIndirectW`、`CreateSolidBrush`、`ExtractIconExW`、`SHGetFileInfoW(SHGFI_ICON)`、`GetDC`、`BeginPaint` が、デストラクタやテーマ／DPI 変更時の再生成前に確実に `DeleteObject`、`DestroyIcon`、`ReleaseDC`、`EndPaint` で解放されているか。
   - **周期ポーリングにおけるキャッシュ肥大化**: 1〜5秒周期の更新ループ内で使われる `std::unordered_map` / `std::map`（`static_cache_`、`previous_times_`、`last_notified_map_`、`IconHelper` のアイコンキャッシュ等）が、終了した PID や不要キーの削除、または LRU 上限によるサイズ制限を行っているか。
   - **ワーカースレッドの終了同期**: バックグラウンドスレッド（`service_manager_service.cc`, `main_window.cc`）がウィンドウ破棄時にキャンセル・join され、未処理の `PostMessageW` ペイロードがリークしないか。

3. **ロジックバグ・実装の正確性 (`BUG` / `IMP`)**
   - **サービス制御と列挙の正確性**: `RestartService` で停止失敗やタイムアウトを無視せず `SERVICE_STOPPED` 到達を確認してから `StartServiceW` を呼んでいるか。`EnumServicesStatusExW` で `ERROR_MORE_DATA` と `resume_handle` をループ処理しているか。
   - **API 失敗と正常な既定値の混同防止**: Win32 API の取得失敗（アクセス拒否等）を無視して `0`、`SYSTEM`、`64-bit`、`Per-Monitor` などの誤解を招く正常値として表示していないか。未実装の列が UI や監視条件に公開されていないか。
   - **多言語対応と原子的保存**: 設定保存時に表示言語に依存した文字列（`含む` と `Contains` など）を書き出していないか。設定やエクスポートの保存が一時ファイル＋フラッシュ＋原子的置換（`ReplaceFileW`）で行われ、失敗時に呼び出し元へエラーを返しているか。
   - **ビルド経路の一貫性とテストの安全性**: `LiteProcManager.vcxproj` と `CMakeLists.txt` の設定が乖離していないか。単体テスト（`tests/lite_proc_manager_test.cc`）が実行ユーザーの実際のレジストリ（`HKCU\...\Run`）を汚染しないか。

---

## ステップ 4: レポート出力フォーマット

監査結果は、以下の構成で分かりやすく日本語で報告してください。

1. **エグゼクティブサマリー**:
   - カテゴリ別（`SEC`: 脆弱性、`LEAK`: メモリ・ハンドルリーク、`BUG`: バグ・正確性）および重要度別（`高` / `中` / `低`）の検出件数。
   - ビルドおよび単体テストの実行結果。
2. **検出された問題の詳細**:
   各問題について以下の項目を記載します。
   - **ID と重要度**: 例 `[SEC-XX / 高]`、`[LEAK-XX / 高]`、`[BUG-XX / 中]`
   - **対象箇所**: クリック可能なファイル・行番号リンク（例: `[main_window.cc](file:///.../src/main_window.cc#L100-L120)`）。
   - **原因と影響**: どのような実行経路・失敗条件でリークやクラッシュ、誤動作が発生するかの具体的な説明。
   - **推奨修正案**: 問題を解決するための具体的な Win32 API パターン、RAII ラッパー、または検証ロジック。
3. **回帰テストの提案**:
   - 再発防止のために `tests/lite_proc_manager_test.cc` に追加すべき単体テストケース（または ASan / Application Verifier での検証シナリオ）。
