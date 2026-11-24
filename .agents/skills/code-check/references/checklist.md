# LiteProcManager 詳細監査チェックリスト

このドキュメントは、LiteProcManager（C++17 / Win32 GUI / プロセス・サービス管理アプリケーション）のアーキテクチャに特化した詳細監査チェックリストです。

---

## 1. セキュリティ・脆弱性チェックリスト (`SEC`)

### 1.1 プロセス同一性と TOCTOU（PID 再利用問題）
- **リスク**: Windows はプロセス終了直後にプロセス ID（PID）を再利用します。`DWORD pid` のみを識別子としてプロセス操作や親子ツリー構築を行うと、確認ダイアログ表示中やスナップショット更新の合間に対象プロセスが終了・PID 再利用された際、無関係な別プロセスを終了・優先度変更してしまう危険があります。
- **チェック項目**:
  - プロセス終了、ツリー終了、優先度変更、および選択状態の復元において、対象ハンドルを開いて `GetProcessTimes()` で取得した作成時刻とスナップショット保持時の `(process_id, creation_time)` が一致することを検証しているか。
  - 親子プロセスツリーの構築時に `親の作成時刻 <= 子の作成時刻` を検証し、かつ `visited` 集合で親 PID の循環参照（無限ループ・スタックオーバーフロー）を遮断しているか。

### 1.2 管理者昇格実行とサブプロセス起動の安全性
- **リスク**: LiteProcManager はシステムプロセスやサービスを操作するため管理者権限（Elevated）で動作することが多くあります。`ShellExecuteW`、`ShellExecuteExW`、`CreateProcessW` に相対実行ファイル名や引用符なしのパスを渡すと、カレントディレクトリや検索パスからの DLL / EXE ハイジャックにより、攻撃者のバイナリを管理者権限で実行してしまう恐れがあります。
- **チェック項目**:
  - `ShellExecuteW`、`ShellExecuteExW`、`CreateProcessW`、`WinExec` の全呼び出し箇所を検査する。
  - 「ファイルの場所を開く」などのシェル操作では、`explorer.exe` を直接起動せず、シェル PIDL API（`ILCreateFromPathW` + `SHOpenFolderAndSelectItems`）を使用しているか。
  - 自プロセスの再起動（`GetModuleFileNameW`）や既知の実行ファイルを起動する際は、完全な絶対パスを使用し、戻り値（`ShellExecuteW` の場合は `reinterpret_cast<INT_PTR>(hInst) > 32`）を検査しているか。
  - Web 検索機能などで URL を開く際、クエリ文字列が UTF-8 変換のうえ RFC 3986 に従ってパーセントエンコード（`include/url_helper.h`）されているか。

### 1.3 単一起動（Single-Instance）Mutex と IPC / ウィンドウメッセージ
- **リスク**: 推測可能な固定名 `Local\...` の Mutex を既定のセキュリティ記述子（DACL）で作成すると、同一セッション内の低権限プロセスに Mutex を先取り（Squatting）されて起動不能（DoS）にされたり、昇格再起動の引き継ぎ時に同名ウィンドウクラスでなりすまされたりするリスクがあります。
- **チェック項目**:
  - `src/main.cc` および `src/main_window.cc` において、`CreateMutexW` / `FindWindowW` による単一起動ロックと管理者再起動時のロック引き継ぎが安全に行われているか。
  - `WM_APP` / `WM_USER` などのカスタムウィンドウメッセージでポインタ（非同期サービス取得結果など）を受け渡す際、外部プロセスからの `PostMessageW` 偽装による任意メモリアクセスを防げる構造になっているか（内部スレッド間での状態管理や世代番号チェックなど）。

### 1.4 外部入力とエクスポートの無害化（JSON / TSV / レジストリ）
- **リスク**: プロセス名、ウィンドウタイトル、コマンドライン、ファイル説明などは他プロセスが任意に制御できる非信頼データです。また、設定 JSON ファイルやレジストリ値も破損・改ざんされている可能性があります。
- **チェック項目**:
  - **TSV / クリップボード出力 (`include/tsv_helper.h`, `src/process_export.cc`)**: 先頭の空白・制御文字を除いて `=`, `+`, `-`, `@` で始まるセルにアポストロフィ `'` を付与し、Excel / LibreOffice での数式インジェクション（CSV/TSV Injection）を防いでいるか。タブや改行が正規化されているか。
  - **設定 JSON (`include/json_helper.h`, `src/app_settings.cc`)**:
    - ファイルサイズ上限と JSON ネスト深さ上限が設定されているか。
    - 数値変換前に `std::isfinite` と整数範囲チェックを行っているか（`RefreshIntervalSeconds * 1000` の符号付き整数オーバーフロー等）。
    - 全列が `Visible: false` にされた設定を読み込んでも `visible_cols[0]` で範囲外参照（クラッシュ）しないよう、最低1列の表示を保証し、列 ID の重複や範囲外の enum 値、異常なウィンドウ寸法・フォントサイズを正規化（クランプ）しているか。
  - **レジストリ (`src/app_settings.cc`, `installer/installer.iss`)**: 自動起動（`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`）の値名がアプリ本体とインストーラーのアンインストール定義で一致しているか。また、読み取り時に `REG_SZ` 型および実実行ファイルパスとの一致を検証しているか。

### 1.5 バイナリ防御設定（コンパイラ・リンカー緩和策）
- **チェック項目**:
  - `src/LiteProcManager.vcxproj`、`tests/LiteProcManagerTests.vcxproj`、`CMakeLists.txt` において、`/W4`、`/sdl`、`/guard:cf`（Control Flow Guard）、`/CETCOMPAT`（Shadow Stack）、`/Qspectre`、DynamicBase（ASLR）、HighEntropyVA、NXCompat が有効になっているか。
  - ビルド後の EXE を `dumpbin /headers` および `dumpbin /loadconfig` で確認し、意図した防御フラグが PE ヘッダーに反映されているか。

---

## 2. メモリ・ハンドル・GDI リークチェックリスト (`LEAK` / `PERF`)

### 2.1 Win32 ウィンドウとオブジェクト寿命（`WM_DESTROY` vs `WM_NCDESTROY`）
- **リスク**: `WM_DESTROY` の処理中に `delete this` を実行すると、その後に必ず送られてくる最終メッセージ `WM_NCDESTROY` で `GWLP_USERDATA` から解放済みポインタを参照してしまい、Use-After-Free（未定義動作・クラッシュ）が発生します。また、`new WindowClass` の後に `CreateWindowExW` が失敗した場合、`WM_NCDESTROY` が送られないためヒープとコンストラクタで確保した GDI リソースがリークします。
- **チェック項目**:
  - `src/toast_window.cc`、`src/main_window.cc`、各種ダイアログにおいて、自己解放を行う場合は `WM_NCDESTROY` で `SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0)` を実行し、`DefWindowProcW` 呼び出し後に `delete this` しているか。
  - `CreateWindowExW` が `nullptr` を返した失敗経路で、確保済みオブジェクトが確実に解放されるか。

### 2.2 Win32 カーネルハンドル・SCM ハンドル・シェルメモリ
- **リスク**: LiteProcManager は 1〜5 秒周期で全プロセス・サービスをポーリングするため、エラー分岐で 1 つでもハンドル閉じ忘れがあると、1 時間あたり数千〜数万のハンドルリークが発生します。
- **チェック項目**:
  - **プロセス・スナップショットハンドル**: `OpenProcess`、`CreateToolhelp32Snapshot`、`OpenProcessToken` が、途中の API 失敗による早期 `return` や `continue` を含む全経路で `CloseHandle` されているか。（※ `OpenProcess` の失敗は `NULL`、`CreateToolhelp32Snapshot` や `CreateFileW` の失敗は `INVALID_HANDLE_VALUE` である点に注意）。
  - **サービス制御マネージャー (SCM)**: `OpenSCManagerW`、`OpenServiceW` が全終了経路で `CloseServiceHandle` されているか。
  - **レジストリキー**: `RegOpenKeyExW`、`RegCreateKeyExW` が全経路で `RegCloseKey` されているか。
  - **シェル・COM メモリ**: `ILCreateFromPathW` に対する `ILFree`、`CoTaskMemAlloc` 等に対する `CoTaskMemFree` が漏れていないか。

### 2.3 GDI および USER オブジェクト（`HFONT`, `HBRUSH`, `HICON`, `HDC`, `HIMAGELIST`）
- **リスク**: Windows プロセスあたりの GDI / USER ハンドル上限（既定 10,000 個）に達すると、描画崩壊やクラッシュが発生します。
- **チェック項目**:
  - `CreateFontIndirectW` / `CreateFontW` / `CreateSolidBrush` で作成したハンドルが、デストラクタやテーマ変更・DPI 変更時の再作成前に `DeleteObject` で解放されているか。
  - `ExtractIconExW` / `SHGetFileInfoW(SHGFI_ICON)` で取得した `HICON` を `ImageList_AddIcon` / `ImageList_ReplaceIcon` に渡した後、元の `HICON` を `DestroyIcon` で破棄しているか（ImageList は内部にコピーを作るため、元の `HICON` の破棄が必要）。
  - `BeginPaint` ↔ `EndPaint`、`GetDC` ↔ `ReleaseDC`、`CreateCompatibleDC` ↔ `DeleteDC` が対になっているか。

### 2.4 周期ポーリングにおけるキャッシュの無制限肥大化
- **リスク**: PID や実行ファイルパス、監視ルールをキーとするマップ（`std::unordered_map`）から古いエントリを削除しないと、長時間稼働時にメモリ使用量が単調増加し続けます。
- **チェック項目**:
  - **静的プロセスメタデータ & CPU 時間差分マップ (`src/process_snapshot_service.cc`)**: スナップショットごとに終了済み PID のエントリが削除されているか。また、キーまたはバリューに `creation_time` を含め、PID 再利用時に古いキャッシュを引き継がないようになっているか。
  - **監視通知履歴マップ (`src/monitor_service.cc`)**: `last_notified_map_` が終了したプロセスや削除・変更されたルールのキーを定期的にパージ（整理）しているか。
  - **アイコンキャッシュ (`src/icon_helper.cc`)**: `HIMAGELIST` とパスキャッシュに上限（LRU 容量制限）があり、`ImageList_ReplaceIcon` でスロットを再利用しているか。

### 2.5 バックグラウンドスレッドと終了時の同期
- **リスク**: ワーカースレッド（サービス一覧の非同期取得など）が動作中にメインウィンドウが破棄されると、破棄済みメンバへのアクセスやスレッドリーク、未処理の `PostMessageW` ペイロードのメモリリークが発生します。
- **チェック項目**:
  - ウィンドウ破棄時（またはデストラクタ）にワーカースレッドへキャンセルを通知し、完了を待機（join）しているか。
  - `PostMessageW` が失敗した場合や、メッセージ処理前にウィンドウが閉じた場合に、動的確保したペイロードがリークしないか。

---

## 3. ロジックバグ・実装の正確性チェックリスト (`BUG` / `IMP`)

### 3.1 Win32 API の状態遷移と部分データの扱い
- **サービス再起動の状態機械 (`src/service_manager_service.cc`)**:
  - サービスの再起動（`RestartService`）において、`ControlService(..., SERVICE_CONTROL_STOP, ...)` の成否を確認しているか。停止タイムアウト後に `SERVICE_STOPPED` になっていないにもかかわらず `StartServiceW` を呼び、`ERROR_SERVICE_ALREADY_RUNNING` を「再起動成功」と誤判定していないか。
  - `EnumServicesStatusExW`（`src/service_enumeration_helper.cc`）において、バッファサイズ変化による `ERROR_MORE_DATA` や `resume_handle` の継続取得をループ処理しているか。
- **API 失敗時のサイレント既定値 (`src/process_snapshot_service.cc`)**:
  - `OpenProcess`（アクセス拒否）、`GetGuiResources`、`IsWow64Process`、`OpenProcessToken` などの失敗時に、正常値と区別がつかない `0`、`SYSTEM`、`64-bit`、`有効` などを表示していないか（「取得不能 / N/A」と実測値を区別できるか）。
- **未実装の公開列 (`include/process_snapshot_service.h`, `src/process_item.cc`)**:
  - `command_line`、`gpu_percent`、`gpu_engine`、`dedicated_gpu_memory`、`shared_gpu_memory`、`enterprise_context`、`dpi_awareness` など、収集処理が未実装のまま UI の列選択や監視ルール条件に公開されている項目がないか。

### 3.2 多言語対応（ローカライズ）と永続化の整合性
- **表示言語に依存した設定保存 (`src/monitor_rule.cc`, `src/app_settings.cc`)**:
  - 比較演算子（`Contains` vs `含む`）や設定 enum を JSON に保存する際、UI 表示用のローカライズ文字列ではなく言語非依存の識別子で保存しているか（また、既存ファイル読み込み時の後方互換性があるか）。
- **実行中の言語切り替え (`src/process_item.cc`, `src/main_window.cc`)**:
  - プロセスキャッシュ内に翻訳済み文字列を保持せず、描画時（`GetColumnValue`）に現在の言語リソースへ変換しているか。

### 3.3 原子的ファイル保存とエラー通知
- **設定ファイル・エクスポート保存 (`src/app_settings.cc`, `src/process_export.cc`)**:
  - 同一ディレクトリの一時ファイルへ書き込み・flush した後に `ReplaceFileW` / `MoveFileExW` で原子的に置換しているか。
  - 保存失敗（ディスクフルや権限エラーなど）を呼び出し元へ返し、ユーザーへ通知できる設計になっているか。

### 3.4 ビルド経路の一貫性とテストの副作用防止
- **MSBuild と CMake の同期 (`LiteProcManager.sln`, `CMakeLists.txt`, `tests/`)**:
  - `.vcxproj` に追加されたソースファイルやコンパイルフラグ、テスト設定が `CMakeLists.txt` 側にも反映されているか。
- **単体テストの隔離 (`tests/lite_proc_manager_test.cc`)**:
  - 単体テストが開発者・実行ユーザーの実際の `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` や本番設定ファイルを書き換えてテスト失敗時に環境を壊すことがないか。
