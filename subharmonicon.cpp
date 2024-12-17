#include "daisy_patch.h"
#include "daisysp.h"
#include <array>
#include <vector>
#include <string>

using namespace daisy;
using namespace daisysp;

// Daisy Patch instance
DaisyPatch patch;

// Constants
constexpr size_t kNumSubharmonics = 4;
constexpr size_t kWaveformBufferSize = 128;
constexpr size_t kNumScales = 3;
constexpr size_t kNumNotes = 12;
constexpr size_t kNumOctaves = 9;

// Display Modes
enum class DisplayMode
{
    WAVEFORM,
    XY,
    MENU
};

enum class MenuState
{
    SCALE_SELECTION,
    ROOT_NOTE_SELECTION
};

// Quantizer Scales
std::vector<std::vector<float>> scales = {
    {0, 2, 4, 5, 7, 9, 11}, // Major
    {0, 2, 3, 5, 7, 8, 10}, // Minor
    {0, 2, 5, 7, 9}         // Pentatonic
};

std::array<std::string, kNumNotes> note_labels = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

size_t current_scale_idx = 0;
int root_note_midi = 69; // Default root note (A4)

// Oscillators
std::array<Oscillator, kNumSubharmonics> subharmonics;
float subharmonic_ratios[kNumSubharmonics] = {2.0f, 3.0f, 4.0f, 5.0f};

// Waveform Buffers
std::array<float, kWaveformBufferSize> osc_buffer_l, osc_buffer_r;
size_t buffer_index = 0;

// UI State
DisplayMode display_mode = DisplayMode::WAVEFORM;
MenuState menu_state = MenuState::SCALE_SELECTION;
bool menu_active = false;

// Helper: Convert MIDI note to frequency
float MidiToFrequency(int midi_note)
{
    return 440.0f * powf(2.0f, (midi_note - 69) / 12.0f);
}

// Helper: Quantize Frequency
float Quantize(float freq)
{
    float midi_note = 12.0f * log2f(freq / 440.0f) + 69.0f; // Convert to MIDI note
    float root_midi = root_note_midi;
    float closest = root_midi;

    for (auto note : scales[current_scale_idx])
    {
        float candidate = floor(midi_note / 12) * 12 + note + root_midi;
        if (abs(midi_note - candidate) < abs(midi_note - closest))
            closest = candidate;
    }
    return MidiToFrequency(closest);
}

// Update Encoder and Menu Navigation
void UpdateEncoder()
{
    if (patch.encoder.RisingEdge())
    {
        menu_active = !menu_active; // Toggle menu
        if (!menu_active)
            display_mode = DisplayMode::WAVEFORM; // Exit to waveform view
    }

    if (menu_active)
    {
        if (patch.encoder.Increment() > 0)
        {
            if (menu_state == MenuState::SCALE_SELECTION)
                current_scale_idx = (current_scale_idx + 1) % kNumScales;
            else if (menu_state == MenuState::ROOT_NOTE_SELECTION)
                root_note_midi = (root_note_midi + 1) % (kNumNotes * kNumOctaves);
        }
        else if (patch.encoder.Increment() < 0)
        {
            if (menu_state == MenuState::SCALE_SELECTION)
                current_scale_idx = (current_scale_idx + kNumScales - 1) % kNumScales;
            else if (menu_state == MenuState::ROOT_NOTE_SELECTION)
                root_note_midi = (root_note_midi + (kNumNotes * kNumOctaves) - 1) % (kNumNotes * kNumOctaves);
        }

        if (patch.encoder.Pressed())
        {
            menu_state = (menu_state == MenuState::SCALE_SELECTION) ? MenuState::ROOT_NOTE_SELECTION
                                                                    : MenuState::SCALE_SELECTION;
        }
    }
    else
    {
        // Toggle between Waveform and XY View
        if (patch.encoder.Increment() != 0)
        {
            display_mode = (display_mode == DisplayMode::WAVEFORM) ? DisplayMode::XY : DisplayMode::WAVEFORM;
        }
    }
}

// Display: Update Screen
void UpdateDisplay()
{
    patch.display.Fill(false);

    if (menu_active)
    {
        patch.display.SetCursor(0, 0);
        patch.display.WriteString("Menu:", Font_7x10, true);

        if (menu_state == MenuState::SCALE_SELECTION)
        {
            patch.display.SetCursor(0, 15);
            std::string scale_name = (current_scale_idx == 0) ? "Major" : (current_scale_idx == 1) ? "Minor"
                                                                                                  : "Pentatonic";
            patch.display.WriteString("Scale: ", Font_7x10, false);
            patch.display.WriteString(scale_name.c_str(), Font_7x10, true);
        }
        else if (menu_state == MenuState::ROOT_NOTE_SELECTION)
        {
            patch.display.SetCursor(0, 15);
            int note_idx = root_note_midi % kNumNotes;
            int octave = root_note_midi / kNumNotes;
            char buf[32];
            sprintf(buf, "Root: %s%d", note_labels[note_idx].c_str(), octave);
            patch.display.WriteString(buf, Font_7x10, true);
        }
    }
    else if (display_mode == DisplayMode::WAVEFORM)
    {
        for (size_t i = 1; i < kWaveformBufferSize; i++)
        {
            int x1 = (i - 1) * (patch.display.Width() / kWaveformBufferSize);
            int y1 = (osc_buffer_l[i - 1] * 20) + 32;
            int x2 = i * (patch.display.Width() / kWaveformBufferSize);
            int y2 = (osc_buffer_l[i] * 20) + 32;
            patch.display.DrawLine(x1, y1, x2, y2, true);
        }
    }
    else if (display_mode == DisplayMode::XY)
    {
        for (size_t i = 1; i < kWaveformBufferSize; i++)
        {
            int x = (osc_buffer_l[i] * 20) + 64;
            int y = (osc_buffer_r[i] * 20) + 32;
            patch.display.DrawPixel(x, y, true);
        }
    }

    patch.display.Update();
}

// Audio Callback
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    UpdateEncoder();

    for (size_t i = 0; i < size; i++)
    {
        float pitch_cv = patch.controls[patch.CTRL_1].Process();
        float freq = Quantize(20.0f + pitch_cv * 1980.0f);

        float mix_l = 0.0f, mix_r = 0.0f;

        for (size_t j = 0; j < kNumSubharmonics; j++)
        {
            subharmonics[j].SetFreq(freq / subharmonic_ratios[j]);
            float sig = subharmonics[j].Process();
            if (j % 2 == 0)
                mix_l += sig;
            else
                mix_r += sig;
        }

        mix_l *= 0.5f;
        mix_r *= 0.5f;

        osc_buffer_l[buffer_index] = mix_l;
        osc_buffer_r[buffer_index] = mix_r;
        buffer_index = (buffer_index + 1) % kWaveformBufferSize;

        out[0][i] = mix_l;
        out[1][i] = mix_r;
    }
}

int main(void)
{
    patch.Init();
    for (auto& osc : subharmonics)
    {
        osc.Init(patch.AudioSampleRate());
        osc.SetWaveform(Oscillator::WAVE_SIN);
    }

    patch.StartAdc();
    patch.StartAudio(AudioCallback);

    while (true)
    {
        UpdateDisplay();
    }
}

