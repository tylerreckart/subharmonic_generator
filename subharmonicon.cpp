#include "daisy_patch.h"
#include "daisysp.h"
#include <array>
#include <vector>
#include <string>
#include <cmath>
#include <cstdio>

using namespace daisy;
using namespace daisysp;

// Enumeration for display modes
enum class DisplayMode
{
    WAVEFORM,
    XY
};

// Enumeration for menu states
enum class MenuState
{
    SCALE_SELECTION,
    ROOT_NOTE_SELECTION
};

// Enumeration for control indices
enum ControlIndex
{
    CTRL_PITCH = 0,
    // Add other controls here if needed
};

// Daisy Patch instance
DaisyPatch patch;

// Constants
constexpr size_t kNumSubharmonics = 4;
constexpr size_t kWaveformBufferSize = 128;
constexpr size_t kNumScales = 25;
constexpr size_t kNumNotes = 12;
constexpr size_t kNumOctaves = 9;

// Quantizer Scales
std::vector<std::vector<float>> scales = {
    {0, 2, 4, 5, 7, 9, 11},       // Major (Ionian)
    {0, 2, 3, 5, 7, 8, 10},       // Minor (Aeolian)
    {0, 2, 5, 7, 9},              // Pentatonic
    {0, 2, 3, 5, 7, 9, 10},       // Dorian
    {0, 1, 3, 5, 7, 8, 10},       // Phrygian
    {0, 2, 4, 6, 7, 9, 11},       // Lydian
    {0, 2, 4, 5, 7, 9, 10},       // Mixolydian
    {0, 1, 3, 5, 6, 8, 10},       // Locrian
    {0, 2, 4, 6, 8, 10},          // Whole Tone
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, // Chromatic
    {0, 3, 5, 6, 7, 10},           // Blues
    {0, 2, 3, 5, 7, 8, 11},        // Harmonic Minor
    {0, 2, 3, 5, 7, 9, 11},        // Melodic Minor
    {0, 1, 4, 5, 7, 8, 11},        // Hungarian Minor
    {0, 1, 4, 5, 7, 8, 10},        // Phrygian Dominant
    {0, 1, 4, 5, 7, 8, 11},        // Double Harmonic
    {0, 1, 3, 6, 7, 10},           // Enigmatic
    {0, 1, 4, 5, 7, 9, 11},        // Persian
    {0, 1, 5, 7, 8, 11},           // Japanese
    {0, 1, 3, 5, 7, 8, 10},        // Neopolitan Minor
    {0, 1, 4, 5, 7, 9, 11},        // Neopolitan Major
    {0, 2, 4, 5, 7, 9, 10, 11},    // Bebop Major
    {0, 2, 3, 5, 7, 9, 10, 11},    // Bebop Minor
    {0, 2, 4, 5, 8, 9, 11},        // Ionian Augmented
    {0, 2, 4, 5, 7, 9, 10}         // Lydian Dominant
};

// Scale Names
std::array<std::string, kNumScales> scale_names = {
    "Major",
    "Minor",
    "Pentatonic",
    "Dorian",
    "Phrygian",
    "Lydian",
    "Mixolydian",
    "Locrian",
    "Whole Tone",
    "Chromatic",
    "Blues",
    "Harmonic Minor",
    "Melodic Minor",
    "Hungarian Minor",
    "Phrygian Dominant",
    "Double Harmonic",
    "Enigmatic",
    "Persian",
    "Japanese",
    "Neopolitan Minor",
    "Neopolitan Major",
    "Bebop Major",
    "Bebop Minor",
    "Ionian Augmented",
    "Lydian Dominant"
};

// Note Labels
std::array<std::string, kNumNotes> note_labels = {
    "C", "C#", "D", "D#", "E", "F",
    "F#", "G", "G#", "A", "A#", "B"
};

// Global Variables
size_t current_scale_idx = 0;
int root_note_midi = 69; // Default root note (A4)

// Oscillators
std::array<Oscillator, kNumSubharmonics> subharmonics;
const float subharmonic_ratios[kNumSubharmonics] = {2.0f, 3.0f, 4.0f, 5.0f};

// Waveform Buffers
std::array<float, kWaveformBufferSize> osc_buffer_l = {0.0f};
std::array<float, kWaveformBufferSize> osc_buffer_r = {0.0f};
size_t buffer_index = 0;

// UI State
DisplayMode display_mode = DisplayMode::WAVEFORM;
MenuState menu_state = MenuState::SCALE_SELECTION;
bool menu_active = false;

// Debounce Variables for Encoder
bool last_encoder_pressed = false;

// Helper: Convert MIDI note to frequency
float MidiToFrequency(int midi_note)
{
    return 440.0f * powf(2.0f, (midi_note - 69) / 12.0f);
}

// Helper: Quantize Frequency
float Quantize(float freq)
{
    float midi_note = 12.0f * log2f(freq / 440.0f) + 69.0f; // Convert to MIDI note
    float root_midi = static_cast<float>(root_note_midi);
    float closest = root_midi;

    for (auto note : scales[current_scale_idx])
    {
        float candidate = floorf(midi_note / 12.0f) * 12.0f + note + root_midi;
        if (std::abs(midi_note - candidate) < std::abs(midi_note - closest))
            closest = candidate;
    }

    // Constrain MIDI note to valid range
    closest = std::fmax(0.0f, std::fmin(127.0f, closest));

    return MidiToFrequency(static_cast<int>(closest));
}

// Update Encoder and Menu Navigation
void UpdateEncoder()
{
    // Handle Encoder Rising Edge to toggle menu
    if (patch.encoder.RisingEdge())
    {
        menu_active = !menu_active; // Toggle menu
        if (!menu_active)
            display_mode = DisplayMode::WAVEFORM; // Exit to waveform view
    }

    // Read encoder increment once to prevent multiple reads
    int encoder_increment = patch.encoder.Increment();

    if (menu_active)
    {
        if (encoder_increment > 0)
        {
            if (menu_state == MenuState::SCALE_SELECTION)
            {
                current_scale_idx = (current_scale_idx + 1) % kNumScales;
            }
            else if (menu_state == MenuState::ROOT_NOTE_SELECTION)
            {
                root_note_midi = (root_note_midi + 1) % (kNumNotes * kNumOctaves);
            }
        }
        else if (encoder_increment < 0)
        {
            if (menu_state == MenuState::SCALE_SELECTION)
            {
                current_scale_idx = (current_scale_idx + kNumScales - 1) % kNumScales;
            }
            else if (menu_state == MenuState::ROOT_NOTE_SELECTION)
            {
                root_note_midi = (root_note_midi + (kNumNotes * kNumOctaves) - 1) % (kNumNotes * kNumOctaves);
            }
        }

        // Handle Encoder Press for menu state toggling with debouncing
        if (patch.encoder.Pressed() && !last_encoder_pressed)
        {
            menu_state = (menu_state == MenuState::SCALE_SELECTION) ? MenuState::ROOT_NOTE_SELECTION
                                                                    : MenuState::SCALE_SELECTION;
            last_encoder_pressed = true;
        }
        else if (!patch.encoder.Pressed())
        {
            last_encoder_pressed = false;
        }
    }
    else
    {
        if (encoder_increment != 0)
        {
            // Toggle between Waveform and XY View
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

        patch.display.SetCursor(0, 15);
        if (menu_state == MenuState::SCALE_SELECTION)
        {
            std::string scale_name = scale_names[current_scale_idx];
            patch.display.WriteString("Scale: ", Font_7x10, false);
            patch.display.WriteString(scale_name.c_str(), Font_7x10, true);
        }
        else if (menu_state == MenuState::ROOT_NOTE_SELECTION)
        {
            int note_idx = root_note_midi % kNumNotes;
            int octave = root_note_midi / kNumNotes;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "Root: %s%d", note_labels[note_idx].c_str(), octave);
            patch.display.WriteString(buf, Font_7x10, true);
        }
    }
    else if (display_mode == DisplayMode::WAVEFORM)
    {
        for (size_t i = 1; i < kWaveformBufferSize; i++)
        {
            int x1 = static_cast<int>((i - 1) * (patch.display.Width() / kWaveformBufferSize));
            int y1 = static_cast<int>((osc_buffer_l[i - 1] * 20.0f) + 32.0f);
            int x2 = static_cast<int>(i * (patch.display.Width() / kWaveformBufferSize));
            int y2 = static_cast<int>((osc_buffer_l[i] * 20.0f) + 32.0f);
            patch.display.DrawLine(x1, y1, x2, y2, true);
        }
    }
    else if (display_mode == DisplayMode::XY)
    {
        for (size_t i = 0; i < kWaveformBufferSize; i++)
        {
            int x = static_cast<int>((osc_buffer_l[i] * 20.0f) + 64.0f);
            int y = static_cast<int>((osc_buffer_r[i] * 20.0f) + 32.0f);
            patch.display.DrawPixel(x, y, true);
        }
    }

    patch.display.Update();
}

// Audio Callback
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        // Process Pitch CV from control
        float pitch_cv = patch.controls[CTRL_PITCH].Process();
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

        // Store in waveform buffers with thread-safe access if needed
        osc_buffer_l[buffer_index] = mix_l;
        osc_buffer_r[buffer_index] = mix_r;
        buffer_index = (buffer_index + 1) % kWaveformBufferSize;

        // Output to audio buffers
        out[0][i] = mix_l;
        out[1][i] = mix_r;
    }
}

int main(void)
{
    // Initialize Patch
    patch.Init();

    // Initialize Oscillators
    for (auto& osc : subharmonics)
    {
        osc.Init(patch.AudioSampleRate());
        osc.SetWaveform(Oscillator::WAVE_SIN);
    }

    // Start ADC and Audio
    patch.StartAdc();
    patch.StartAudio(AudioCallback);

    // Main Loop
    while (true)
    {
        UpdateEncoder();  // Handle encoder input in main loop
        UpdateDisplay();  // Update display in main loop
        delay(1);
    }
}
