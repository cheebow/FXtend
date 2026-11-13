# FXtend

MIDI Note On を 1 個置くだけで、DAW のテンポに同期した長さの Impact / Riser / Downlifter / Sweep / Clap / Sub Drop を発音する FX 専用インストゥルメント（VST3 / AU、macOS 12 以降、Apple Silicon / Intel）。

- 長さは小節・拍で指定し、ホストの音楽的位置（PPQ）で測る。MIDI ノートの長さには左右されない。
- 内蔵のシンセ（Noise / Sub / FM / Transient / Clap）だけで鳴る。Factory プリセット 61 種は外部ファイル不要。
- 自分の WAV / AIFF を 4 つまで重ねられる（Stretch / Resample / Loop / One-shot）。

仕様: [`FXtend_SPEC.md`](FXtend_SPEC.md)　／　テスト計画と DAW での確認手順: [`docs/TEST_PLAN.md`](docs/TEST_PLAN.md)

## ビルド

必要なもの: Xcode、CMake 3.24 以降。JUCE 9、Rubber Band、Catch2 は構成時に自動取得される。

```sh
cmake -B build -G Xcode
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

ビルドすると `~/Library/Audio/Plug-Ins/VST3` と `~/Library/Audio/Plug-Ins/Components` にプラグインがコピーされる（無効にするには `-DFXTEND_COPY_PLUGIN_AFTER_BUILD=OFF`）。スタンドアロン版は `build/FXtend_artefacts/Release/Standalone/`。

## 配布用インストーラ

```sh
scripts/package-macos.sh
```

Apple Silicon と Intel の両対応でビルドし、VST3・AU・スタンドアロン版を入れるインストーラを `build-dist/package/` に作る。インストール先は `/Library/Audio/Plug-Ins/VST3`、`/Library/Audio/Plug-Ins/Components`、`/Applications`。

他の Mac で開けるようにするには署名と公証が必要で、環境変数で指定する（指定しなければ署名なしで作る）。

| 環境変数 | 内容 |
|---|---|
| `SIGN_APP` | `Developer ID Application: 名前 (チームID)`。プラグインとアプリの署名 |
| `SIGN_PKG` | `Developer ID Installer: 名前 (チームID)`。インストーラの署名 |
| `NOTARY_PROFILE` | `xcrun notarytool store-credentials` で作ったプロファイル名。Apple の公証に出してチケットを付ける |

### アンインストール

次を削除する（ユーザープリセットとサンプルも消す場合は最後の 1 つも）。

```text
/Library/Audio/Plug-Ins/VST3/FXtend.vst3
/Library/Audio/Plug-Ins/Components/FXtend.component
/Applications/FXtend.app
~/Library/Application Support/FXtend
```

## 使い方

1. インストゥルメントトラックに読み込み、プリセットか FX タイプを選ぶ。
2. `LENGTH` で長さ（例: 4 Bars）を決める。右に小節・四分音符・秒の換算が出る。
3. 鳴らしたい位置に MIDI ノートを 1 個置く。ノートの高さと長さは音に影響しない（ベロシティは音量）。
4. `TAIL`: `Strict` は終点で完全に無音、`Natural` はリバーブ／ディレイの余韻だけ残す。
5. `Advanced` で各レイヤーと Master を細かく編集できる。`Export WAV` で現在のテンポ・長さのままファイルに書き出せる。

ホストがテンポを報告しないとき（スタンドアロンなど）は `MANUAL BPM` の値を使い、表示が `MANUAL` になる。

## データの保存場所

| 内容 | 場所 |
|---|---|
| ユーザープリセット（`.fxpreset`、JSON） | `~/Library/Application Support/FXtend/Presets` |
| 読み込んだサンプルの管理コピー | `~/Library/Application Support/FXtend/Samples` |

サンプルは読み込み時に内容のハッシュ名で管理フォルダへコピーされ、プロジェクトやプリセットにはその参照だけが保存される。元ファイルを移動しても同じ Mac では再生できる。別の Mac へ持っていくときは `File > Export Preset Package...` でプリセットとサンプルを 1 ファイル（`.fxtendpack`）にまとめる。読み込むサンプルの利用権限は各自で確認すること。

## 構成

```text
src/engine/   ホスト・UI から独立した発音エンジン
                TransportClock  ホストの PPQ / BPM / 拍子 → ブロックごとのタイムライン
                FxVoice         1 回のトリガー。進捗 = (PPQ - 開始PPQ) / 長さ、終点サンプルで終了
                SynthLayer / SampleLayer / MasterFx / FxTemplates（FX タイプとマクロ）
                OfflineRenderer / WavExport
src/dsp/      ノイズ、フィルター、オシレーター、カーブ、タイムストレッチ（Rubber Band）
src/sample/   サンプルの取り込みと管理コピー
src/preset/   プリセットの JSON 形式、Factory プリセット、ユーザープリセット、パッケージ
src/ui/       エディタの部品
src/          プラグイン層（AudioProcessor / Editor / ホストパラメータ）
tests/        偽ホストでエンジンとプラグインを駆動する自動テスト
```

レイヤーや Master の設定項目は `src/engine/LayerFields.cpp` の表 1 か所で定義され、ホストパラメータ・プリセット・編集画面がそこから作られる。**表の id と選択肢の順序は保存データの互換性に関わるので、追加は末尾に行い、既存のものは変更しない。**

## 仕様からの変更点・既知の制限

- **テンポ変化中の Stretch**: 一定テンポでは Rubber Band（R3）で事前レンダーした音を再生する。再生中にテンポが変わった場合や、新しい長さのレンダーが終わるまでの間は、終点の一致を優先して速度可変で再生する（その間は音程がテンポ比だけずれる）。リアルタイムのストレッチ処理は行っていない。
- Overlap 中に Strict を使うと、Master のリバーブ／ディレイは最後に鳴っている本体の終点で無音になる（Master FX は全ボイス共通のため）。
- Custom 4-point カーブはエンジンとプリセットが対応しているが、編集画面はまだない。
- ノート番号による音程・プリセット切り替え、DAW への WAV ドラッグ、Windows / Intel は未対応。
- `auval` と pluginval（strictness 10、VST3 / AU）は通過。Cubase / Logic での実機確認（`docs/TEST_PLAN.md`）はこれから。

## ライセンス

GNU Affero General Public License v3.0（[`LICENSE`](LICENSE)）。

- [JUCE](https://juce.com) 9 を AGPLv3 の条件で利用。
- [Rubber Band Library](https://breakfastquay.com/rubberband/) を GPL（v2 以降）の条件で利用。

どちらも構成時に取得され、このリポジトリには含まれない。インストーラに入れる表記は [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。製品名「FXtend」について正式な商標調査は行っていない（ウェブ検索で同名の音楽ソフトが見つからないことのみ確認）。

## ロゴとアイコン

`assets/logo/` にある。マーク（消えていく音と立ち上がる音が向かい合って X の形になる絵）、ロゴ（暗い背景用・明るい背景用）、アプリのアイコン。SVG が元データで、PNG はそこから書き出したもの。ロゴの文字は [Outfit](https://github.com/Outfitio/Outfit-Fonts)（SIL Open Font License 1.1）を図形に変換したもので、フォントがなくても同じ形で表示される。プラグイン画面左上のロゴも同じ図形を描いている。
