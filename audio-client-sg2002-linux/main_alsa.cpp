#include <iostream>
#include <alsa/asoundlib.h>
#include <cstring>
#include <cmath>

extern "C" {
#include "wakeword.h"  // TFLite wakeword core
}

const int SAMPLE_RATE = 16000;
const int FRAME_SIZE = 160;  // 10ms at 16kHz
const int NUM_CHANNELS = 1;
const int BYTES_PER_SAMPLE = 2;

// ALSA capture parameters
snd_pcm_t *capture_handle = nullptr;
snd_pcm_hw_params_t *hw_params = nullptr;

bool initialize_alsa_capture(const char *device_name = "hw:0,0") {
    int err;
    
    // Open capture device
    err = snd_pcm_open(&capture_handle, device_name, SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
        std::cerr << "Error opening ALSA device '" << device_name << "': "
                  << snd_strerror(err) << std::endl;
        return false;
    }
    
    // Allocate hardware parameters
    snd_pcm_hw_params_alloca(&hw_params);
    err = snd_pcm_hw_params_any(capture_handle, hw_params);
    if (err < 0) {
        std::cerr << "Error getting hardware parameters: " << snd_strerror(err) << std::endl;
        return false;
    }
    
    // Set interleaved access mode
    err = snd_pcm_hw_params_set_access(capture_handle, hw_params, 
                                        SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err < 0) {
        std::cerr << "Error setting access mode: " << snd_strerror(err) << std::endl;
        return false;
    }
    
    // Set sample format (16-bit signed)
    err = snd_pcm_hw_params_set_format(capture_handle, hw_params, SND_PCM_FORMAT_S16_LE);
    if (err < 0) {
        std::cerr << "Error setting sample format: " << snd_strerror(err) << std::endl;
        return false;
    }
    
    // Set number of channels
    err = snd_pcm_hw_params_set_channels(capture_handle, hw_params, NUM_CHANNELS);
    if (err < 0) {
        std::cerr << "Error setting channels: " << snd_strerror(err) << std::endl;
        return false;
    }
    
    // Set sample rate
    unsigned int rate = SAMPLE_RATE;
    err = snd_pcm_hw_params_set_rate_near(capture_handle, hw_params, &rate, nullptr);
    if (err < 0) {
        std::cerr << "Error setting sample rate: " << snd_strerror(err) << std::endl;
        return false;
    }
    std::cout << "Sample rate set to: " << rate << " Hz" << std::endl;
    
    // Set buffer size
    snd_pcm_uframes_t buffer_size = FRAME_SIZE * 4;
    err = snd_pcm_hw_params_set_buffer_size_near(capture_handle, hw_params, &buffer_size);
    if (err < 0) {
        std::cerr << "Error setting buffer size: " << snd_strerror(err) << std::endl;
        return false;
    }
    
    // Apply parameters
    err = snd_pcm_hw_params(capture_handle, hw_params);
    if (err < 0) {
        std::cerr << "Error applying hardware parameters: " << snd_strerror(err) << std::endl;
        return false;
    }
    
    // Prepare capture device
    err = snd_pcm_prepare(capture_handle);
    if (err < 0) {
        std::cerr << "Error preparing PCM device: " << snd_strerror(err) << std::endl;
        return false;
    }
    
    std::cout << "ALSA capture initialized successfully on " << device_name << std::endl;
    return true;
}

bool capture_frame(int16_t *buffer, int num_samples) {
    snd_pcm_sframes_t frames_read;
    
    frames_read = snd_pcm_readi(capture_handle, buffer, num_samples);
    
    if (frames_read < 0) {
        if (frames_read == -EPIPE) {
            // Underrun, recover
            std::cerr << "ALSA underrun, recovering..." << std::endl;
            snd_pcm_recover(capture_handle, frames_read, 1);
            return false;
        } else {
            std::cerr << "Error reading from PCM device: " << snd_strerror(frames_read) << std::endl;
            return false;
        }
    }
    
    if (frames_read != num_samples) {
        std::cerr << "Warning: requested " << num_samples << " frames, got " 
                  << frames_read << " frames" << std::endl;
    }
    
    return true;
}

void cleanup_alsa() {
    if (capture_handle) {
        snd_pcm_drain(capture_handle);
        snd_pcm_close(capture_handle);
        capture_handle = nullptr;
    }
}

int main(int argc, char *argv[]) {
    float threshold = 0.995f;
    int max_duration_sec = 0;  // 0 = infinite
    std::string device = "hw:0,0";
    
    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--threshold" && i + 1 < argc) {
            threshold = std::stof(argv[++i]);
        } else if (std::string(argv[i]) == "--device" && i + 1 < argc) {
            device = argv[++i];
        } else if (std::string(argv[i]) == "--duration" && i + 1 < argc) {
            max_duration_sec = std::stoi(argv[++i]);
        } else if (std::string(argv[i]) == "--help") {
            std::cout << "Usage: audio_wakeword_linux [options]\n"
                      << "Options:\n"
                      << "  --device <hw:card,device>  ALSA device (default: hw:0,0)\n"
                      << "  --threshold <0-1>          Detection threshold (default: 0.995)\n"
                      << "  --duration <seconds>       Max recording duration (default: infinite)\n"
                      << "  --help                     Show this help\n";
            return 0;
        }
    }
    
    std::cout << "=== INMP441 I2S Wakeword Detector (ALSA) ===" << std::endl;
    std::cout << "Device: " << device << std::endl;
    std::cout << "Threshold: " << threshold << std::endl;
    
    // Initialize ALSA
    if (!initialize_alsa_capture(device.c_str())) {
        std::cerr << "Failed to initialize ALSA capture" << std::endl;
        return 1;
    }
    
    // Initialize wakeword engine
    if (!wakeword_init()) {
        std::cerr << "Failed to initialize wakeword engine" << std::endl;
        cleanup_alsa();
        return 1;
    }
    
    // Capture and process audio
    int16_t frame_buffer[FRAME_SIZE];
    int frames_captured = 0;
    int wakeword_detections = 0;
    
    std::cout << "\nListening... (Press Ctrl+C to stop)" << std::endl;
    std::cout << "Waiting for wakeword (threshold: " << threshold << ")" << std::endl;
    
    while (true) {
        // Check duration limit
        if (max_duration_sec > 0 && frames_captured >= max_duration_sec * SAMPLE_RATE) {
            std::cout << "\nMax duration reached (" << max_duration_sec << "s). Stopping." << std::endl;
            break;
        }
        
        // Capture one frame (10ms)
        if (!capture_frame(frame_buffer, FRAME_SIZE)) {
            continue;  // Skip on error and try again
        }
        
        // Feed to wakeword detector
        float confidence = wakeword_feed((int16_t *)frame_buffer, FRAME_SIZE);
        
        // Check for detection
        if (confidence > threshold) {
            wakeword_detections++;
            std::cout << "[" << frames_captured << "ms] WAKEWORD DETECTED! "
                      << "Confidence: " << confidence << std::endl;
            wakeword_reset();
        } else if (frames_captured % (SAMPLE_RATE / FRAME_SIZE) == 0) {
            // Print status every second
            std::cout << "[" << (frames_captured / SAMPLE_RATE) << "s] "
                      << "Confidence: " << confidence << std::endl;
        }
        
        frames_captured += FRAME_SIZE;
    }
    
    // Cleanup
    wakeword_reset();
    cleanup_alsa();
    
    std::cout << "\n=== Statistics ===" << std::endl;
    std::cout << "Total frames captured: " << frames_captured << std::endl;
    std::cout << "Total duration: " << (frames_captured / SAMPLE_RATE) << " seconds" << std::endl;
    std::cout << "Wakeword detections: " << wakeword_detections << std::endl;
    
    return 0;
}
