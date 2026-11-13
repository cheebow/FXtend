# FXtend 開発仕様書

- 版: 0.1（設計案）
- 目的: VST3 / AU の「FX専用インストゥルメント」。MIDI Note On を配置するだけで、DAWのテンポに同期した長さのImpact / Riser / Downlifter / Sweep / Clap等を発音する。
- 主な対象: macOS / Apple Silicon / Cubase 15（VST3）、Logic Pro（AU）
- 開発方針: **シンセ合成だけで完結する**。必要に応じてユーザーが用意したサンプルを重ねられる。配布時に第三者サンプルは必須にしない。

## 1. 中核コンセプトと優先順位

1. **タイミングの正確さが最優先**: FXの本体は「MIDI Note On の位置から、プラグインで指定した長さ（例: 4小節）まで」を基準に動作する。MIDIノートの保持時間では決めない。
2. **DAWテンポ同期**: 再生テンポと拍子を読み、指定した拍／小節数をホストの音楽的位置で測る。終点で各レイヤーの本体を終了する。
3. **ゼロからのFX生成**: サンプル無しでも使える内蔵のNoise・Sub・FM・Clap等のDSP合成を用意する。
4. **ユーザーサンプルの統合**: WAV / AIFFを読み込み、音程を維持するタイムストレッチや再生速度変更などで指定長に合わせられる。
5. **曲作り中の速さ**: プリセット選択→MIDIを1個置く→即使える。複雑なモジュラー編集は初版では不要。

## 2. 製品形態 / 対応範囲

| 項目 | 初版仕様 |
|---|---|
| 種類 | ソフトウェア音源（Instrument / Synth）。通常のオーディオインサートエフェクトではない |
| 形式 | VST3、AU（macOS） |
| OS / CPU | macOS Apple Silicon優先。Intel / Windows VST3は後続で検討 |
| 入出力 | MIDI入力、ステレオ音声出力。音声入力・MIDI出力なし |
| ホスト | まずCubase 15 VST3で実機検証。AUはLogic Proで検証 |
| サンプリング周波数 | 44.1 / 48 / 88.2 / 96kHzで検証 |
| プロジェクト | C++20 / JUCE / CMake / Xcode |
| 動作 | リアルタイム再生およびDAWのオフライン書き出し |

## 3. MIDIトリガー / 長さ / 終端（最重要）

### 3.1 トリガー

- `Note On (velocity > 0)` で現在選択中のFXを再生開始。
- ノート番号は初版では音程に影響しない（全鍵盤同じFX）。Velocityは出力レベルに適用。将来のノート別プリセット割り当ては拡張機能。
- `Note Off` は標準設定では**発音時間に影響しない**。短いMIDIノートでも、指定が4小節なら4小節発音する。
- ノート開始はMIDIイベントのサンプルオフセットを厳密に尊重。既定では自動クオンタイズしない。
- 同じFXの再トリガーは初版で`Restart`（前の本体を短くフェードアウトして新規発音）を既定とする。`Overlap`（最大8ボイス）も選択可。

### 3.2 長さの指定

- `Length Unit`: Bars / Beats / Custom Quarter-Notes。
- プリセット: 1/2・1・2・4・8小節、1/2・1・2・4拍。カスタムは0.25～32四分音符相当から、最大32小節までの範囲で設定可能とする（UI上は拍子に合わせ換算）。
- `Bars` は現在の拍子に従う。たとえば4/4の1小節=四分音符4個、6/8の1小節=四分音符3個。
- `Beats` は拍子の分母の音価を基準とする（4/4の1拍=四分音符、6/8の1拍=八分音符）。曖昧さを避けるためUIに「X bars / Y quarter-notes / Z s」を併記する。
- 秒数は参考表示であり、**再生終了位置はホストの音楽的位置（PPQ）を真値とする**。
- ホストBPM/拍子取得不能時は手動BPM=120、拍子=4/4をフォールバックとし、その旨を画面に表示する。

### 3.3 終点

- `FX Body End` = `Note On開始PPQ + 長さ（四分音符換算）`。
- RiserはBody End直前に強さ/ピッチ/フィルター等の指定ピークへ到達、Downlifterは指定の最低値へ到達、Sweepは指定カーブの終点へ到達する。
- Body Endでドライ音／シンセ本体／サンプル本体を終了。クリックを避ける極短フェードを適用（主に終点直前に収める）。
- `Tail Mode`: `Strict`（Body Endで完全無音） / `Natural`（Body End後にリバーブ・ディレイの余韻のみ残す）。初期値はFX種類別。TailはBodyと明確に分けて波形表示する。
- Note Offを無視するのは標準モード。将来の`Gate`モードは別途拡張し、既定動作を変更しない。

### 3.4 テンポ変化、停止、ループ、オフライン処理

- エンジン内部の経過量は原則`elapsedQuarterNotes`で管理する。毎処理ブロックでホストPPQ/BPM・サンプルオフセットを用い、0～1の正規化進捗`phase`を算出する。
- ホスト再生中のテンポ変更やテンポオートメーションにもBodyの終端位置が音楽的位置として追従する。大きなテンポジャンプではサンプルストレッチの音質が変化し得るが、終点の整合を優先する。
- ホスト位置の不連続ジャンプ（シーク、巻戻し、ループ境界）を検出したときは、アクティブな声をクリアする。ループ再入時のMIDI Note Onで再発音する。
- ホスト停止時はアクティブな声を停止。ホスト停止中の鍵盤クリック／プレビューは手動BPMを使った独立時間軸で再生。
- オフラインバウンスと通常再生で同じ終了位置になること。
- 仕様上の終了位置は「FX本体」であり、Natural Tailは終点後に残ってよい。

**計算例**: BPM 128 / 4/4 / 4小節 = `4 × 4 × (60 / 128) = 7.5秒`。ただしBPMが途中で変われば必要秒数も変わり、終点はあくまで4小節後。

## 4. 音源エンジン

### 4.1 全体シグナルフロー

```text
MIDI Note On + Host Transport
             |
     Beat-based Timeline (0..1)
             |
 +-----------+-------------+
 |                         |
Synth Layers (最大4)   Sample Layers (最大4)
 |                         |
 | Noise / Sub / FM         | WAV / AIFF
 | Clap / Transient         | Stretch / Loop / Reverse
 +-----------+-------------+
             |
      Layer Mixer
    Gain / Pan / Offset
             |
      Master Effects
   EQ/Filter -> Drive ->
   Delay/Reverb -> Limiter
             |
      Stereo Audio Out
```

- シンセとサンプルは同じ正規化タイムラインに接続する。
- 各レイヤーに`On/Off`, `Level`, `Pan`, `Start Offset`, `End/Envelope`, `Pitch`, `Stereo Width`を持つ。個別の長さ比率は0～100%のBody区間として設定可。
- 音源レイヤーがなくても無音ではなく、各Factory FXプリセットは必ずシンセだけで発音する。

### 4.2 シンセモジュール（初版）

| モジュール | DSP構成 | 主用途 |
|---|---|---|
| Noise | White / Pink / Brown Noise + LP/HP/BP Filter | Riser, Downlifter, Sweep, Whoosh |
| Sub | Sine / Triangle Oscillator + Pitch Envelope | Impact, Sub Drop |
| FM / Metallic | 2-operator FM + Decay Envelope | Impact, Metallic Hit |
| Transient | 短いNoise Burst、Click、複数発音 | Attack |
| Clap | 3～6個のノイズバースト + Body Noise + EQ | Clap, Long Clap |

**共通カーブ**: `Linear`, `Exponential`, `S-curve`, `Custom 4-point`（カーブ編集UIはフェーズ2）を、ピッチ・フィルター・音量にそれぞれ設定可能とする。

### 4.3 FXタイプ別マクロ

| FXタイプ | 大きな操作ノブ | 終点の意味 |
|---|---|---|
| Impact | Punch / Sub / Brightness / Tail | アタックは開始点。残響の収束点がBody End |
| Riser | Rise / Tension / Brightness / Width | 上昇カーブのピークがBody End |
| Downlifter | Fall / Depth / Air / Width | 下降カーブの最低点がBody End |
| Sweep | Direction / Cutoff Range / Noise Color | フィルター移動完了がBody End |
| Clap / Long Clap | Density / Spread / Body / Tail | クラップ本体とテールの終了点がBody End（Naturalでは残響だけ延長可） |
| Sub Drop | Start Pitch / End Pitch / Weight / Drive | 指定音程への下降完了がBody End |

初版のFXタイプは「異なる固定シンセ」ではなく、共通レイヤーエンジンに対するテンプレートとマクロのセットとする。

## 5. ユーザーサンプル・レイヤー

### 5.1 入力・保持

- まずWAV / AIFF（モノ / ステレオ、16/24/32-bit PCM / floatを想定）に対応。異なるサンプルレートは内部変換。
- ファイル選択とファイルのドラッグ＆ドロップに対応。
- 最大4レイヤー、1素材あたり最大60秒を初版の安全上限とする（後で変更可能）。
- 素材読み込み、波形ピーク解析、ストレッチ初期化は**オーディオスレッドで行わない**。
- 読み込みサンプルに対して`Trim Start`, `Trim End`, `Reverse`, `Fade In/Out`, `Gain`, `Pitch`, `Start Offset`を用意。

### 5.2 長さ調整モード

1. **Stretch（既定）**: 音程を原則維持して、トリムしたサンプルの区間を指定FX長へタイムストレッチ。
2. **Resample**: 再生速度と音程を同時に変え、指定FX長に合致させる。特殊効果用。
3. **Loop**: 指定区間をクロスフェードループさせ、Body Endで停止。ノイズ系向け。
4. **One-shot / Envelope**: 原音の速さで再生し、Body Endでゲート。短い打撃音向け。サンプルが早く終わっても無理に引き伸ばさない。

- 初版ではピッチを維持するStretchを最重要品質項目とする。極端な比率（例: 0.25倍未満 / 8倍超）は警告表示し、無理な高音質を約束しない。
- **音楽的な終点の正確さとストレッチ音質は別の要件**として検証する。境界は必ず終了し、伸縮アルゴリズムは音質を改善する。
- BPM一定時の高品質ストレッチを初版の受け入れ必須条件とし、テンポ変化時は追従を優先して可変比率で再生する。切替の音質劣化を軽減するため短時間のクロスフェード／比率平滑化を行う。
- 第三者ストレッチエンジンの採否は配布ライセンスを確認して決める。Rubber Bandを採用する場合、GPL互換の公開または商用ライセンスが必要。ライセンス未確定なら依存せず、DSPストレッチのプロトタイプを自作して品質評価する。

### 5.3 サンプルの保存と再現性

- 読み込み時、元ファイルをユーザー管理領域にハッシュ識別子付きでコピーし、元ファイル移動後も同じMacで再生可能にする。
- ホストのプラグイン状態にはパラメータ、サンプルID/パス/ハッシュを保存。大容量PCMを毎回ホスト状態に丸ごと埋め込まない。
- 他PCへの移動用に`Export Preset Package`を用意し、プリセットJSONと必要サンプルを一括保存。`Import Preset Package`で復元。
- サンプルが見つからない場合は黙って別音へ置換せず、`Missing Sample`を表示して再リンクを求める。シンセレイヤーは再生継続。
- これらのサンプルはユーザー自身が利用権限を確認する。製品には第三者素材を無断同梱しない。

## 6. マスター処理

- 2段のEQ/Filter（HP / LP / Tilt等）、Soft Drive、Stereo Width、Tempo Delay、Algorithmic Reverb、Soft Limiter。
- Reverb/Delayは`Body処理`と`Tail処理`を区別し、Strictモードでは終点で必ず無音、Naturalでは余韻だけ残る。
- Master Gain、Peak Meter、簡易クリップ表示。初版の目標は**ミックスにすぐ使える音**であり、最終ラウドネス保証機能は不要。

## 7. UI仕様

### 7.1 メイン画面（単一ウィンドウ）

```text
+------------------------------------------------------------+
| FX SYNTH                 Preset [Impact 01 v]   Save Random |
| BPM 128 (HOST)     4/4     LENGTH [4 BARS v]     7.50 sec |
|------------------------------------------------------------|
|        WAVE / PHASE PREVIEW                [BODY END |TAIL] |
|   0% ---------------------------------------------- 100%    |
|------------------------------------------------------------|
| FX TYPE: [Impact] [Riser] [Downlifter] [Sweep] [Clap] ...  |
|  Punch    Depth    Brightness    Movement       Tail        |
|------------------------------------------------------------|
| SYNTH: [1 Noise] [2 Sub] [3 FM] [4 Transient]              |
| SAMPLE: [+ Load WAV] [slot2] [slot3] [slot4]                |
|         Mode [Stretch v]  Trim  Reverse  Pitch  Mix         |
|------------------------------------------------------------|
| [Advanced]   [Tail Strict/Natural]   [Preview] [Export WAV]|
+------------------------------------------------------------+
```

- **最も目立つ操作は`FX Type / Length / Preset / Preview`**。内部4+4レイヤーは折りたたみで隠せる。
- `BPM: HOST`/`MANUAL`を表示。`4 bars = 7.50 sec`のように秒も示すが、DAWテンポが変われば表示を更新。
- 波形プレビューは生成結果の概略（レイヤー合成／サンプル）を表示し、Body EndとTailの区間を分ける。
- `Random`は現在のFXタイプを維持して主要パラメータだけ変化させる。`Undo Random`を設ける。
- パラメータはホストからオートメーション可能。値域・単位・初期値を安定化し、保存後の互換性を意識する。
- `Preview`はDAW停止時でも動作し、手動BPMまたは取得できるHOST BPMで発音する。

## 8. プリセット

- 工場出荷プリセットは完全合成のみで最低30種を目標: Impact 8、Riser 6、Downlifter 6、Sweep/Whoosh 4、Clap/Long Clap 4、Sub Drop 2。
- プリセットはJSON相当の構造化データ（バージョン情報付き）。本体はJUCE ValueTreeなどで管理可能。
- ユーザー保存/読み込み/名前変更。Sample参照があるものはパッケージ書き出しも可能。
- 工場プリセットで外部オーディオファイルは一切必須としない。

## 9. WAVエクスポート

- プラグイン内部の「Render FX」で指定長+必要なNatural Tailを非リアルタイムに生成し、WAV 24-bit PCMまたは32-bit float（44.1/48/96kHz等）で保存。
- エクスポート対象は現在のFXプリセットで、Velocity 100相当を既定とする（変更可）。
- レンダーのBPM/拍子はホスト取得値を使い、停止中は手動値を使う。固定BPMの書き出しを初版対象にする。
- **DAWへのWAVドラッグ＆ドロップ転送**は操作性が高いが、VST/AUホスト間の挙動差を要検証のため後続段階に配置。初版はファイル保存を必須とする。

## 10. 実装構成（推奨）

```text
FXtend/
  CMakeLists.txt
  README.md
  docs/
    SPEC.md
    TEST_PLAN.md
  src/
    PluginProcessor.h/.cpp
    PluginEditor.h/.cpp
    engine/
      TransportClock.h/.cpp      # BPM/拍子/PPQとサンプルオフセット
      FxVoice.h/.cpp             # 各トリガーの進捗と終了
      FxEngine.h/.cpp            # Voice管理とレイヤー合算
      SynthLayer.h/.cpp
      SampleLayer.h/.cpp
      LayerMixer.h/.cpp
      MasterFx.h/.cpp
    dsp/
      NoiseGenerator.h/.cpp
      SubOscillator.h/.cpp
      FmGenerator.h/.cpp
      ClapGenerator.h/.cpp
      EnvelopeCurve.h/.cpp
      TimeStretchProcessor.h/.cpp
    sample/
      SampleManager.h/.cpp
      SampleCache.h/.cpp
    preset/
      PresetManager.h/.cpp
      PresetSerializer.h/.cpp
    ui/
      MainView.h/.cpp
      TimelineView.h/.cpp
      LayerEditor.h/.cpp
      WaveformView.h/.cpp
  tests/
    TimingTests.cpp
    AudioRegressionTests.cpp
    PresetTests.cpp
```

- メインAudio ThreadでディスクI/O、重いメモリ確保、ロック待ちを行わない。ファイル解析・サンプルリサンプリング準備は別スレッドで行い、安全なバッファ交換を行う。
- 内部DSPをプラグインUIやホストから独立させ、スタンドアロンのテストハーネスで合成結果とタイミングを検証できるようにする。
- パラメータは`AudioProcessorValueTreeState`などでホスト管理・オートメーションを実現する。

## 11. テスト／受け入れ条件

### 必須テスト

1. **MIDI終点**: 128 BPM、4/4、4小節に設定。短いC3 Note On/Offを与えてもFX Bodyは7.5秒の時点で終了。
2. **サンプル位置精度**: 同一バッファ内に途中から来るMIDIイベントのサンプルオフセットを尊重する（ブロック先頭に丸めない）。
3. **速度変更**: ホストBPMを128→100にして再生し直すと、4小節は9.6秒に変わる。
4. **中途テンポ変更**: 再生中にテンポが変わってもPPQ上の指定終点でBodyを止める（サンプルStretchの音質は別途評価）。
5. **Note Off無視**: Note Off時点で中断しない。
6. **Tail**: Strictは終点後に実質無音、Naturalは余韻だけ残る。
7. **サンプル**: 3秒のWAVをロード→BPM128の4小節にStretch→7.5秒のBodyに収まる。音程維持は聴感／周波数解析で評価。
8. **プロジェクト再現**: 保存→DAW終了→再起動で同じFXを再生。ソースファイル移動後も管理コピーから復元できる。
9. **オフライン書き出し**: リアルタイムと同じMIDI位置・終了位置で音が出る。
10. **例外**: サンプル欠損、テンポ情報欠損、ホストのロケート／ループ、連続リトリガー、44100/48000/96000Hz、バッファ32～1024に耐える。
11. **RT安全性**: 音声コールバック中の明示的なディスクI/O、待機ロック、巨大メモリ確保を避ける。十分なCPU余裕とグリッチ無しを計測。

### 品質ゲート

- VST3をCubase 15のInstrument Trackにロードできる。
- AUをLogic ProのSoftware Instrumentにロードできる。
- アウトプットの頭出しとBody終端がDAWの拍／小節グリッドに合う。
- 音量・フィルター・ピッチのカーブがクリック無しに動作する。
- バッファサイズに依存して音色やタイミングが極端に変わらない。
- サンプルStretchはRiser/Downlifterの長いノイズ系素材で破綻しないことを最低ラインに、Impact/Clapのトランジェント保持を別途評価する。

## 12. 開発フェーズ

- **Phase 0 — 検証**: JUCE VST3/AU起動、Cubase/LogicでホストBPM/PPQを取得、MIDI Note Onのサンプルオフセットと終点を確認。ここが通るまでGUIを作り込まない。
- **Phase 1 — 完全シンセMVP**: 1ボイス→複数ボイス、Noise/Sub/Clap/FM、Riser・Downlifter・Impact・Sweep、1/2/4/8小節、30プリセットの前段階。
- **Phase 2 — サンプルレイヤー**: WAV/AIFF、最大4レイヤー、トリム/逆再生、Stretch/Resample/Loop/One-shot、キャッシュ、素材再リンク。
- **Phase 3 — 仕上げ**: Long Clap・Sub Drop、Master FX、WAV書き出し、プリセットパッケージ、UI polish、オフラインレンダー／自動テスト。
- **Later**: MIDIノート別プリセット、細密カーブ編集、FX内部リサンプリング、DAWへのドラッグWAV、Windows VST3。

## 13. 技術面の注意と未確定項目

- JUCEのホストPositionInfoに含まれるPPQ、BPM、拍子は**ホスト次第で欠損する**。フォールバックとDAW実測が必須。
- JUCEのWAV/AIFF読み込みは標準で構成可能。詳細はAudioFormatManagerを使う。
- 第三者製ストレッチライブラリ（例: Rubber Band）のGPL / 商用条件、JUCE本体のAGPL / 商用条件を、配布形態を決める時点で確認する。
- Phase 0の結果が出るまで「途中テンポ変化で高品質なストレッチを維持」と「VST3/AU双方で全て同一挙動」を保証しない。
- 製品名は FXtend、配布ライセンスは AGPL-3.0、ロゴは `assets/logo/` のものに決定した。FXtend について正式な商標調査は行っていない。

## 14. 参考（公式）

- JUCE CMake API: https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md
- JUCE AudioPlayHead PositionInfo: https://docs.juce.com/master/classjuce_1_1AudioPlayHead_1_1PositionInfo.html
- JUCE AudioFormatManager: https://docs.juce.com/master/classjuce_1_1AudioFormatManager.html
- Rubber Band time stretching / license: https://breakfastquay.com/rubberband/index.html / https://breakfastquay.com/rubberband/license.html
- JUCE license: https://juce.com/get-juce/
