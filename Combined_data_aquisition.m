clear;
clc;

% ESP32 IP Address (Replace with your ESP32's actual IP)
esp32_ip = '172.20.10.4';
data_url = sprintf('http://%s/data', esp32_ip);
counter_url = sprintf('http://%s/updateCounter', esp32_ip);

% Parameters
fs = 100;             % Sampling frequency (Hz)
T = 2;                % Time window (s)
N = fs * T;           % Number of samples per window
threshold = 2000;     % Contact mic threshold
counter = 0;          % Event counter
wasAboveThreshold = false;  % Threshold flag

% Data buffer
data = zeros(1, N);

% Design Butterworth bandpass filters
[b1, a1] = butter(4, [5 20] / (fs/2), 'bandpass');     % 5-20 Hz
[b2, a2] = butter(4, [80 120] / (fs/2), 'bandpass');   % 80-120 Hz

% Plot setup
figure;
subplot(2,1,1);
h1 = plot((1:N)/fs, data, 'b');
xlabel('Time (s)');
ylabel('Amplitude');
title('Filtered Contact Mic Signal (5-20Hz + 80-120Hz)');
grid on;

subplot(2,1,2);
h2 = plot(zeros(1, N/2+1), 'r');
xlabel('Frequency (Hz)');
ylabel('|P1(f)|');
title('Real-Time FFT of Filtered Signal');
grid on;

f = fs * (0:(N/2)) / N;

disp('📡 Starting real-time acquisition...');
fprintf('📈 Threshold: %d\n\n', threshold);

while true
    % Shift buffer
    data(1:end-1) = data(2:end);

    % Read contact mic sample
    try
        raw_data = webread(data_url);
        new_sample = str2double(raw_data);
    catch
        warning('Failed to read data from ESP32.');
        new_sample = 0;
    end

    % Update buffer
    data(end) = new_sample;

    % Apply both filters
    filtered1 = filtfilt(b1, a1, data);
    filtered2 = filtfilt(b2, a2, data);
    filtered_data = filtered1 + filtered2;

    % Detect threshold on original signal (not filtered)
    if new_sample > threshold && ~wasAboveThreshold
        counter = counter + 1;
        wasAboveThreshold = true;

        % Send updated counter to ESP32
        try
            options = weboptions('RequestMethod', 'post', 'Timeout', 2);
            response = webwrite(counter_url, 'counter', num2str(counter), options);
            fprintf('📤 Counter sent: %d\n', counter);
        catch
            warning('Failed to send counter to ESP32.');
        end
    elseif new_sample <= threshold
        wasAboveThreshold = false;
    end

    % Update plots
    set(h1, 'YData', filtered_data);

    fft_data = fft(filtered_data);
    P2 = abs(fft_data / N);
    P1 = P2(1:N/2+1);
    P1(2:end-1) = 2 * P1(2:end-1);
    set(h2, 'YData', P1);

    pause(1/fs);
end
