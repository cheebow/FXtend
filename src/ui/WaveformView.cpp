#include "WaveformView.h"

namespace fxui
{

namespace
{
    // The overview only needs the shape, so very long FX are rendered at a
    // faster tempo. Everything that follows the FX progress scales with it.
    constexpr double maxPreviewSeconds = 12.0;
    constexpr double previewSampleRate = 44100.0;
}

WaveformView::WaveformView() : juce::Thread ("FXtend preview")
{
    setOpaque (false);
    startThread (juce::Thread::Priority::low);
}

WaveformView::~WaveformView()
{
    cancelPendingUpdate();
    signalThreadShouldExit();
    notify();
    stopThread (4000);
}

void WaveformView::setRequest (const fxtend::RenderRequest& request)
{
    {
        const juce::ScopedLock scoped (lock);
        pendingRequest = request;
        hasPending = true;
    }

    notify();
}

void WaveformView::setPlayhead (double phase)
{
    if (std::abs (phase - playhead) > 1.0e-4)
    {
        playhead = phase;
        repaint();
    }
}

void WaveformView::setAccent (juce::Colour colour)
{
    if (accent != colour)
    {
        accent = colour;
        repaint();
    }
}

void WaveformView::run()
{
    while (! threadShouldExit())
    {
        fxtend::RenderRequest request;
        bool work = false;

        {
            const juce::ScopedLock scoped (lock);

            if (hasPending)
            {
                request = pendingRequest;
                hasPending = false;
                work = true;
            }
        }

        if (! work)
        {
            wait (-1);
            continue;
        }

        request.sampleRate = previewSampleRate;

        const double bodySeconds = fxtend::getBodyLengthInSamples (request) / request.sampleRate;

        if (bodySeconds > maxPreviewSeconds)
            request.bpm *= bodySeconds / maxPreviewSeconds;

        const auto result = fxtend::renderOffline (request, [this]
        {
            const juce::ScopedLock scoped (lock);
            return threadShouldExit() || hasPending;
        });

        if (! result.completed)
            continue;

        Overview overview;
        overview.minimum.assign ((size_t) numColumns, 0.0f);
        overview.maximum.assign ((size_t) numColumns, 0.0f);

        const int total = result.audio.getNumSamples();
        overview.bodyFraction = total > 0 ? (double) result.bodySamples / total : 1.0;

        const float* left = result.audio.getReadPointer (0);
        const float* right = result.audio.getReadPointer (1);

        for (int column = 0; column < numColumns && total > 0; ++column)
        {
            const int from = (int) ((juce::int64) column * total / numColumns);
            const int to = juce::jmax (from + 1, (int) ((juce::int64) (column + 1) * total / numColumns));
            float low = 0.0f, high = 0.0f;

            for (int n = from; n < juce::jmin (to, total); ++n)
            {
                const float mono = 0.5f * (left[n] + right[n]);
                low = juce::jmin (low, mono);
                high = juce::jmax (high, mono);
            }

            overview.minimum[(size_t) column] = low;
            overview.maximum[(size_t) column] = high;
        }

        overview.valid = true;

        {
            const juce::ScopedLock scoped (lock);
            finished = std::move (overview);
        }

        triggerAsyncUpdate();
    }
}

void WaveformView::handleAsyncUpdate()
{
    {
        const juce::ScopedLock scoped (lock);
        shown = finished;
    }

    repaint();
}

void WaveformView::setAudition (const juce::String& presetName)
{
    if (auditionName != presetName)
    {
        auditionName = presetName;
        repaint();
    }
}

void WaveformView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    paintPanel (g, bounds);

    const auto area = bounds.reduced (10.0f, 8.0f).withTrimmedBottom (14.0f);
    const float bodyRight = area.getX() + area.getWidth() * (float) shown.bodyFraction;
    const float centreY = area.getCentreY();

    // Tail region
    if (shown.bodyFraction < 0.999)
    {
        g.setColour (Colours::control.withAlpha (0.45f));
        g.fillRect (juce::Rectangle<float> (bodyRight, area.getY(), area.getRight() - bodyRight, area.getHeight()));
    }

    g.setColour (Colours::outline.withAlpha (0.6f));
    g.drawHorizontalLine ((int) centreY, area.getX(), area.getRight());

    for (int i = 1; i < 4; ++i)
    {
        const float x = area.getX() + (bodyRight - area.getX()) * (float) i / 4.0f;
        g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
    }

    if (shown.valid)
    {
        float peak = 0.05f;

        for (size_t i = 0; i < shown.maximum.size(); ++i)
            peak = juce::jmax (peak, shown.maximum[i], -shown.minimum[i]);

        const float scale = area.getHeight() * 0.5f / juce::jmax (1.0f, peak);
        const float columnWidth = area.getWidth() / (float) numColumns;

        juce::Path body, tail;

        for (int column = 0; column < numColumns; ++column)
        {
            const float x = area.getX() + (float) column * columnWidth;
            const float top = centreY - shown.maximum[(size_t) column] * scale;
            const float bottom = centreY - shown.minimum[(size_t) column] * scale;
            (x < bodyRight ? body : tail).addRectangle (x, top, columnWidth + 0.5f, juce::jmax (1.0f, bottom - top));
        }

        g.setGradientFill (juce::ColourGradient (accent.brighter (0.25f), 0.0f, centreY, accent.withAlpha (0.55f), 0.0f, area.getY(), false));
        g.fillPath (body);
        // The tail is the same sound ringing on, so it is drawn only a little dimmer.
        g.setColour (accent.withAlpha (0.72f));
        g.fillPath (tail);
    }

    // Body End marker
    // A quiet dashed line: it marks a point in time, the sound does not stop here.
    {
        const float dashes[] = { 3.0f, 4.0f };
        g.setColour (Colours::text.withAlpha (0.55f));
        g.drawDashedLine ({ bodyRight, area.getY() - 2.0f, bodyRight, area.getBottom() + 2.0f }, dashes, 2, 1.0f);
    }

    g.setFont (font (10.5f));
    g.setColour (Colours::dimText);
    const auto labels = bounds.reduced (10.0f, 4.0f).removeFromBottom (14.0f);
    g.drawText ("0%", labels, juce::Justification::centredLeft);

    // The audition label always sits at the left, after "0%".
    const juce::String auditionNote = auditionName + "  (not loaded)";
    const auto auditionTagFont = font (9.5f, true);
    const auto auditionNoteFont = font (11.0f, true);
    const float auditionTagWidth = juce::GlyphArrangement::getStringWidth (auditionTagFont, "AUDITION") + 10.0f;
    const float auditionWidth = auditionTagWidth + juce::GlyphArrangement::getStringWidth (auditionNoteFont, auditionNote) + 8.0f;
    const float auditionRight = auditionName.isNotEmpty() ? labels.getX() + 30.0f + auditionWidth : labels.getX();

    g.setFont (font (10.5f));
    g.setColour (Colours::dimText);

    if (shown.bodyFraction < 0.999 && bodyRight - 70.0f < auditionRight + 6.0f)
    {
        // A short body: its labels would sit under the audition label, so
        // only "TAIL" is kept if it has room (the dashed line still marks the place).
        if (bodyRight + 4.0f > auditionRight + 6.0f)
            g.drawText ("TAIL", juce::Rectangle<float> (bodyRight + 4.0f, labels.getY(), 60.0f, labels.getHeight()), juce::Justification::centredLeft);
    }
    else if (shown.bodyFraction < 0.999)
    {
        g.drawText ("BODY END", juce::Rectangle<float> (bodyRight - 70.0f, labels.getY(), 66.0f, labels.getHeight()), juce::Justification::centredRight);
        g.drawText ("TAIL", juce::Rectangle<float> (bodyRight + 4.0f, labels.getY(), 60.0f, labels.getHeight()), juce::Justification::centredLeft);
    }
    else
    {
        g.drawText ("100% BODY END", labels, juce::Justification::centredRight);
    }

    if (auditionName.isNotEmpty())
    {
        // Named in the label row under the picture, so nothing covers the
        // waveform, and framed with dashes: a preset being tried, not the loaded one.
        auto row = labels.withWidth (auditionWidth).withX (labels.getX() + 30.0f);

        g.setColour (accent);
        g.fillRoundedRectangle (row.removeFromLeft (auditionTagWidth).reduced (0.0f, 0.5f), 3.0f);
        g.setColour (Colours::background);
        g.setFont (auditionTagFont);
        g.drawText ("AUDITION", row.withX (row.getX() - auditionTagWidth).withWidth (auditionTagWidth), juce::Justification::centred);
        g.setColour (Colours::text);
        g.setFont (auditionNoteFont);
        g.drawText (auditionNote, row, juce::Justification::centredRight);

        juce::Path frame;
        frame.addRoundedRectangle (bounds.reduced (1.5f), 8.0f);
        const float dashes[] = { 6.0f, 5.0f };
        juce::Path dashed;
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, frame, dashes, 2);
        g.setColour (accent.withAlpha (0.9f));
        g.fillPath (dashed);
    }

    if (playhead >= 0.0 && playhead <= 1.0)
    {
        const float x = area.getX() + (bodyRight - area.getX()) * (float) playhead;
        g.setColour (juce::Colours::white);
        g.drawLine (x, area.getY(), x, area.getBottom(), 1.5f);
    }
}

} // namespace fxui
