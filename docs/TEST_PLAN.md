# FXtend テスト計画

仕様は [`FXtend_SPEC.md`](../FXtend_SPEC.md) の 11 章。自動テストで確認できるものと、DAW 実機でしか確認できないものを分けて記載する。

## 自動テスト

```sh
cmake -B build -G Xcode
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

テストは偽のホスト（`tests/TestHost.h`）と偽の PlayHead（`tests/PluginTestSupport.h`）でエンジンとプラグイン本体を駆動する。ユーザーフォルダには触れず、一時フォルダを使う。

| SPEC 11 の必須テスト | 自動テスト |
|---|---|
| 1. MIDI終点（128 BPM / 4小節 = 7.5 秒） | `TimingTests` "A short note still plays the full 4 bars…"、`PluginTests` "The plugin ends its body on the bar line…" |
| 2. サンプル位置精度 | `TimingTests` "Note On sample offsets inside a block are honoured" |
| 3. 速度変更（100 BPM = 9.6 秒） | `TimingTests` "Changing the tempo changes the length in seconds…"、`PluginTests` 同上 |
| 4. 中途テンポ変更 | `TimingTests` "A tempo change during the body…" / "A tempo ramp still ends…"、`SampleTests` "Phase-locked sample layers end on the bar line…" |
| 5. Note Off 無視 | 1 と同じテスト（短いノート＋Note Off） |
| 6. Tail（Strict / Natural） | `TailTests` 一式 |
| 7. サンプル（3 秒 WAV → 4 小節 Stretch、音程維持） | `SampleFileTests` "A 3 s WAV in Stretch mode fills 4 bars at 128 BPM at its original pitch"（ゼロクロスで 440 Hz ±0.5% を確認） |
| 8. プロジェクト再現（保存→復元、ソース移動後） | `PluginTests` "Saving and restoring the plugin state…"、`SampleFileTests` "Projects find their samples again…"（DAW 再起動そのものは手動） |
| 9. オフライン書き出し＝リアルタイム | `TailTests` "Offline render matches real-time playback…"、`AudioRegressionTests` "Real-time playback and offline rendering give the same audio…" |
| 10. 例外（サンプル欠損、テンポ情報欠損、ロケート／ループ、連続リトリガー、SR、バッファ） | `SampleFileTests`（欠損）、`TimingTests` "While stopped, notes play against the manual tempo" / "Seeking or looping…" / "Rapid retriggers…" / "End point is the same for every buffer size and sample rate"、`AudioRegressionTests` |
| 11. RT安全性 | `RealtimeTests`（音声コールバック中のヒープ確保が 0 回であること、8 ボイス重ね時の処理速度） |

そのほか: プリセット（`PresetTests`）、Master FX（`TailTests`）、WAV 書き出しとプリセットパッケージ（`ExportTests`）。

AU の形式検証:

```sh
auval -v aumu Fxtd Chbw
```

確認用の出力（通常のテスト実行には含まれない）:

```sh
# 各 FX タイプを WAV に書き出して聴く
FXTEND_RENDER_DIR=/path/to/folder ./build/Release/FXtendTests "[.render]"

# エディタ画面を PNG に書き出す
FXTEND_RENDER_DIR=/path/to/folder ./build/Release/FXtendTests "[.snapshot]"
```

## 実機確認（手動）

自動テストでは確認できないのは「実際のホストが何を報告してくるか」と「音そのもの」。

### A. タイミング — Cubase 15（VST3）

1. Instrument Track に `FXtend` を読み込む。BPM 表示が `HOST`、拍子に `(default)` が付かないこと。
2. テンポ 128 / 4/4、Length = 4 Bars。小節頭に短い MIDI ノートを 1 個置いて再生。
   - ノート位置から鳴り始め、**4 小節後の小節線ちょうどで止まる**（ノートの長さに関係なく）。
   - 波形プレビュー上を再生位置の線が進み、Body End で止まる。
3. ノートを拍の途中（例: 16 分音符ずらし）に置く → 終端も同じだけずれる。
4. テンポを 100 に変更 → 4 小節後で止まる（表示は 9.60 s）。
5. テンポトラックで 4 小節の途中にテンポ変更を入れる → それでも 4 小節後の小節線で止まる。
6. 鳴っている途中で停止 → 本体はすぐ止まる（Natural の余韻は残ってよい）。
7. 鳴っている途中でロケート → 本体が止まる。
8. 2 小節のサイクル範囲でループ再生、ループ頭にノート → 周回ごとに頭から鳴り直し、前の周回の本体が残らない。
9. 停止中に `Preview` ／鍵盤クリック → 鳴り、Length 分で止まる。
10. オーディオミックスダウン → 波形の開始位置と Body 終端がリアルタイム再生と同じ小節位置。
11. バッファサイズ 32 / 256 / 1024 で 2 の結果が変わらない。
12. 拍子を 6/8 にする → `1 bar / 3 quarter notes` と表示され、その長さで止まる。

### B. タイミング — Logic Pro（AU）

Software Instrument トラックに読み込み（AU Instruments > CHEEBOW > FXtend）、A の 2〜11 を同様に確認（サイクル範囲でのループ、バウンスを含む）。

### C. 音とUI

1. Factory プリセット 30 種を一通り鳴らす。クリックノイズ、極端な音量差、意図しない無音がないか。
2. FX タイプのボタンを切り替え、4 つのマクロノブが名前どおりに効くか。`Random` → `Undo`。
3. Tail を Strict / Natural で切り替え、Impact / Clap で余韻の有無を聴き比べる。波形プレビューに TAIL 区間が出る。
4. Advanced を開き、レイヤーの On/Off、Type、各カーブを触って音が追従するか。オートメーションを書いて再生できるか。
5. Master タブ: フィルター、Drive、Delay、Reverb、Limiter。メーターとクリップ表示（クリックで解除）。

### D. サンプル

1. WAV / AIFF をスロットへドラッグ＆ドロップ、または `+ Load WAV` で読み込む。
2. Stretch: 3 秒程度の素材を 4 小節に。`stretching...` 表示が消えたら再生し、音程が保たれ小節線で終わること。テンポを変えて再度確認。
3. Resample / Loop / One-shot をそれぞれ確認。Trim、Reverse、Pitch、Mix。
4. ノイズ系の長い素材（Riser/Downlifter 向け）で Stretch が破綻しないか。Impact / Clap 素材のトランジェントの残り方。
5. 極端な長さ（例: 0.2 秒の素材を 8 小節）で `extreme stretch` の警告が出ること。
6. プロジェクトを保存 → 元の WAV を別フォルダへ移動 → DAW を再起動して開く → 同じ音が鳴る。
7. `~/Library/Application Support/FXtend/Samples` の該当ファイルも消した状態で開く → スロットに `Missing:` と表示され、シンセレイヤーは鳴る。`Load...` で同じファイルを指定すると復帰する。
8. `File > Export Preset Package...` → 別の Mac（または Samples フォルダを空にした状態）で `Import Preset Package...` → サンプル込みで復元される。

### E. 書き出し

1. `Export WAV` で 24-bit / 48 kHz を保存し、DAW に読み込んでテンポグリッドに合うか（長さ = Length、Natural の場合は余韻付き）。
2. 32-bit float / 96 kHz、Velocity 64 / 127 も確認。

### 結果として知りたいこと

- 終端が小節線からずれる場合、どのくらい・どちら向きか（ホスト名とバッファサイズも）。
- 停止中に BPM が `HOST` と表示されるか `MANUAL` か。
- ループ境界で本体が残る、または鳴り直さないケースがあるか。
- VST3 と AU で挙動が違う点。

## 形式検証の結果（2026-11-12）

| 検証 | 結果 |
|---|---|
| `auval -v aumu Fxtd Chbw` | 成功 |
| pluginval 1.0.4、VST3、strictness 10 | 成功 |
| pluginval 1.0.4、AU、strictness 10 | 成功（警告 1 件: 初期のプログラム番号が -1 と報告される。動作への影響なし） |
| 自動テストを AddressSanitizer + UBSan 付き Debug ビルドで実行 | 59 件すべて成功、検出なし |
| ThreadSanitizer 付き Debug ビルドで、プラグイン層・サンプル・書き出し・リアルタイム系の 19 件を実行 | 成功、検出なし（全件は所要時間が長すぎるため未実施） |

```sh
/Applications/pluginval.app/Contents/MacOS/pluginval --strictness-level 10 --validate-in-process \
    --validate "$HOME/Library/Audio/Plug-Ins/VST3/FXtend.vst3"
```

Factory プリセットのレベル一覧（ピーク、RMS、DC、最大のサンプル間段差とその位置、余韻の長さ）:

```sh
./build/Release/FXtendTests "[.levels]"
```

## まだ行っていない検証

- Steinberg の VST3 `validator`（VST3 SDK のビルドが必要。pluginval 内の該当項目はパス未設定のためスキップされた）。
- Cubase 15 / Logic Pro での実機確認（上の A〜E）。
- Intel Mac / Windows でのビルド（初版の対象外）。
