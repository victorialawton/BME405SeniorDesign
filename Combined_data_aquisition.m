clear;
clc;

% ==== SETUP ====

% ESP32 IP Address — replace this with your ESP32's local IP
esp32_ip = '172.20.10.4';

% Construct full URLs for data fetching and counter updating
data_url = sprintf('http://%s/data', esp32_ip);
counter_url = sprintf('http://%s/updateCounter', esp32_ip);

% Sampling parameters
fs = 100;              % Sampling frequency in Hz
T = 2;                 % Time window in seconds
N = fs * T;            % Total number of samples per window

% Thresholding parameters
threshold = 2000;      % Raw contact mic amplitude threshold to detect events
counter = 0;           % Event counter to track threshold crossings
wasAboveThreshold = false;  % Flag to detect rising edge only

% Preallocate buffer for incoming samples
data = zeros(1, N);    % Ring buffer of size N

% ==== FILTER DESIGN ====

% Design two 4th-order Butterworth bandpass filters:
%   - First filter for bruxism-like low frequency: 5–20 Hz
%   - Second filter for TMJ click-like high frequency: 80–120 Hz
[b1, a1] = butter(4, [5 20] / (fs/2), 'bandpass');
[b2, a2] = butter(4, [80 120] / (fs/2), 'bandpass');

% ==== PLOTTING SETUP ====

% Set up real-time plot window
figure;

% Time-domain plot (filtered signal)
subplot(2,1,1);
h1 = plot((1:N)/fs, data, 'b');  % Pre-allocate plot line
xlabel('Time (s)');
ylabel('Amplitude');
title('Filtered Contact Mic Signal (5-20Hz + 80-120Hz)');
grid on;

% Frequency-domain plot (FFT of filtered signal)
subplot(2,1,2);
h2 = plot(zeros(1, N/2+1), 'r');  % Pre-allocate plot line
xlabel('Frequency (Hz)');
ylabel('|P1(f)|');
title('Real-Time FFT of Filtered Signal');
grid on;

% Frequency axis for FFT plot
f = fs * (0:(N/2)) / N;

% Display startup message
disp('📡 Starting real-time acquisition...');
fprintf('📈 Threshold: %d\n\n', threshold);

% ==== MAIN LOOP ====

while true
    % Shift all data left to make room for new sample (rolling buffer)
    data(1:end-1) = data(2:end);

    % Try to read one new sample from ESP32 over HTTP
    try
        raw_data = webread(data_url);          % Request latest data point
        new_sample = str2double(raw_data);     % Convert string to numeric
    catch
        warning('Failed to read data from ESP32.');
        new_sample = 0;                         % Use 0 if fetch fails
    end

    % Insert new sample at end of buffer
    data(end) = new_sample;

    % Apply both bandpass filters to the buffer
    filtered1 = filtfilt(b1, a1, data);         % Low-frequency filter
    filtered2 = filtfilt(b2, a2, data);         % High-frequency filter
    filtered_data = filtered1 + filtered2;      % Combine both bands

    % Check if new sample crosses the threshold from below
    if new_sample > threshold && ~wasAboveThreshold
        counter = counter + 1;                  % Increment counter
        wasAboveThreshold = true;               % Avoid multiple triggers

        % Send updated counter to ESP32 via POST request
        try
            options = weboptions('RequestMethod', 'post', 'Timeout', 2);
            response = webwrite(counter_url, 'counter', num2str(counter), options);
            fprintf('📤 Counter sent: %d\n', counter);
        catch
            warning('Failed to send counter to ESP32.');
        end
    elseif new_sample <= threshold
        wasAboveThreshold = false;              % Reset flag if below threshold
    end

    % ==== UPDATE PLOTS ====

    % Update time-domain plot
    set(h1, 'YData', filtered_data);

    % Compute FFT of filtered data
    fft_data = fft(filtered_data);
    P2 = abs(fft_data / N);         % Two-sided spectrum
    P1 = P2(1:N/2+1);               % One-sided spectrum
    P1(2:end-1) = 2 * P1(2:end-1);  % Compensate for dropped half

    % Update frequency-domain plot
    set(h2, 'YData', P1);

    % Wait for next sampling interval (simulate real-time loop)
    pause(1/fs);
end
