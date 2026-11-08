using System;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Windows.Forms;
using NAudio.CoreAudioApi;
using NAudio.Wave;
using NAudio.Wave.SampleProviders;

sealed class DeviceChoice
{
    public string Id, Name;
    public override string ToString() { return Name; }
}

sealed class SynthMonitor : Form
{
    readonly MMDeviceEnumerator devices = new MMDeviceEnumerator();
    readonly ComboBox input = new ComboBox(), output = new ComboBox();
    readonly Button monitor = new Button(), send = new Button();
    readonly Label status = new Label();
    readonly TrackBar volume = new TrackBar();
    readonly Timer discovery = new Timer();
    volatile WasapiCapture capture;
    volatile WasapiOut playback, samplePlayer;
    BufferedWaveProvider monitorBuffer;
    SampleChannel monitorGain;
    WaveFileReader sampleFile;
    MMDevice captureDevice, playbackDevice, sampleDevice;
    volatile bool closing;

    public SynthMonitor()
    {
        Text = "STM32 Synth Monitor";
        ClientSize = new Size(600, 370);
        MinimumSize = new Size(616, 409);
        Font = new Font("Segoe UI", 10);
        BackColor = Color.FromArgb(18, 24, 34);
        ForeColor = Color.White;
        Controls.Add(new Label { Text = "STM32 SYNTH", Left = 24, Top = 20, Width = 540, Height = 32,
            Font = new Font("Segoe UI", 19, FontStyle.Bold) });
        AddLabel("Synth audio input", 65);
        ConfigureCombo(input, 90);
        AddLabel("Play through these speakers / headphones", 133);
        ConfigureCombo(output, 158);
        monitor.SetBounds(24, 204, 265, 36);
        monitor.BackColor = Color.FromArgb(35, 105, 160); monitor.ForeColor = Color.White;
        monitor.FlatStyle = FlatStyle.Flat; monitor.UseVisualStyleBackColor = false;
        monitor.Text = "Start monitoring";
        monitor.Click += delegate { if (capture == null) StartMonitoring(); else StopMonitoring(); };
        send.SetBounds(307, 204, 265, 36);
        send.BackColor = Color.FromArgb(35, 65, 90); send.ForeColor = Color.White;
        send.FlatStyle = FlatStyle.Flat; send.UseVisualStyleBackColor = false;
        send.Text = "Send WAV to synth...";
        send.Click += delegate { SendSample(); };
        Controls.Add(monitor); Controls.Add(send);
        AddLabel("Monitor volume", 255);
        volume.SetBounds(170, 247, 402, 42); volume.Minimum = 0; volume.Maximum = 100; volume.Value = 30;
        volume.TickFrequency = 10;
        volume.ValueChanged += delegate { if (monitorGain != null) monitorGain.Volume = volume.Value / 100f; };
        Controls.Add(volume);
        status.SetBounds(24, 298, 552, 58);
        status.Text = "Connect the board in Synth mode, then choose an output.";
        Controls.Add(status);
        RefreshDevices();
        discovery.Interval = 2000;
        discovery.Tick += delegate { if (capture == null && samplePlayer == null) RefreshDevices(); };
        discovery.Start();
        FormClosing += delegate { closing = true; discovery.Stop(); StopMonitoring(); StopSample(); devices.Dispose(); };
    }

    void AddLabel(string text, int y)
    {
        Controls.Add(new Label { Text = text, Left = 24, Top = y, Width = 550, Height = 24 });
    }

    void ConfigureCombo(ComboBox box, int y)
    {
        box.BackColor = Color.FromArgb(35, 45, 60); box.ForeColor = Color.White;
        box.SetBounds(24, y, 548, 30); box.DropDownStyle = ComboBoxStyle.DropDownList; Controls.Add(box);
    }

    void FillDevices(ComboBox box, DataFlow flow, bool synthOnly)
    {
        string selected = box.SelectedItem is DeviceChoice ? ((DeviceChoice)box.SelectedItem).Id : null;
        var choices = devices.EnumerateAudioEndPoints(flow, DeviceState.Active)
            .Where(d => synthOnly ? d.FriendlyName.IndexOf("Synth", StringComparison.OrdinalIgnoreCase) >= 0 :
                                   d.FriendlyName.IndexOf("Synth", StringComparison.OrdinalIgnoreCase) < 0)
            .Select(d => new DeviceChoice { Id = d.ID, Name = d.FriendlyName }).ToArray();
        if (box.Items.Cast<DeviceChoice>().Select(d => d.Id).SequenceEqual(choices.Select(d => d.Id))) return;
        box.Items.Clear(); box.Items.AddRange(choices);
        int index = Array.FindIndex(choices, d => d.Id == selected);
        box.SelectedIndex = index >= 0 ? index : choices.Length > 0 ? 0 : -1;
    }

    void RefreshDevices()
    {
        try { FillDevices(input, DataFlow.Capture, true); FillDevices(output, DataFlow.Render, false); }
        catch (Exception ex) { status.Text = "Audio device discovery: " + ex.Message; }
    }

    void StartMonitoring()
    {
        if (input.SelectedItem == null || output.SelectedItem == null)
        { status.Text = "A synth input and a speaker output are both required."; return; }
        try
        {
            captureDevice = devices.GetDevice(((DeviceChoice)input.SelectedItem).Id);
            playbackDevice = devices.GetDevice(((DeviceChoice)output.SelectedItem).Id);
            capture = new WasapiCapture(captureDevice);
            monitorBuffer = new BufferedWaveProvider(capture.WaveFormat) {
                BufferDuration = TimeSpan.FromMilliseconds(250), DiscardOnBufferOverflow = true, ReadFully = true
            };
            monitorGain = new SampleChannel(monitorBuffer, false) { Volume = volume.Value / 100f };
            playback = new WasapiOut(playbackDevice, AudioClientShareMode.Shared, true, 50);
            playback.Init(new SampleToWaveProvider(monitorGain));
            playback.PlaybackStopped += MonitorPlaybackStopped;
            capture.DataAvailable += Captured;
            capture.RecordingStopped += CaptureStopped;
            playback.Play(); capture.StartRecording();
            monitor.Text = "Stop monitoring";
            input.Enabled = output.Enabled = false;
            status.Text = "Monitoring. Play the touchscreen keyboard or enable TONE 440.";
        }
        catch (Exception ex) { StopMonitoring(); status.Text = "Cannot start monitoring: " + ex.Message; }
    }

    void Captured(object sender, WaveInEventArgs e)
    {
        if (closing || !ReferenceEquals(sender, capture)) return;
        var buffer = monitorBuffer;
        if (buffer != null) buffer.AddSamples(e.Buffer, 0, e.BytesRecorded);
    }

    void QueueForCurrentStream(object sender, Func<object> currentStream, Action action)
    {
        if (closing || IsDisposed || !IsHandleCreated || !ReferenceEquals(sender, currentStream())) return;
        try
        {
            BeginInvoke((Action)delegate {
                if (!closing && !IsDisposed && ReferenceEquals(sender, currentStream())) action();
            });
        }
        catch (InvalidOperationException) { /* The window handle may be destroyed while closing. */ }
    }

    void CaptureStopped(object sender, StoppedEventArgs e)
    {
        QueueForCurrentStream(sender, delegate { return capture; }, delegate {
            StopMonitoring();
            status.Text = e.Exception == null ? "Monitoring stopped." : "Audio connection ended: " + e.Exception.Message;
        });
    }

    void MonitorPlaybackStopped(object sender, StoppedEventArgs e)
    {
        QueueForCurrentStream(sender, delegate { return playback; }, delegate {
            StopMonitoring();
            status.Text = e.Exception == null ? "Monitoring stopped." : "Speaker connection ended: " + e.Exception.Message;
        });
    }

    void StopMonitoring()
    {
        var old = capture; capture = null;
        var oldPlayback = playback; playback = null;
        if (old != null)
        {
            old.DataAvailable -= Captured; old.RecordingStopped -= CaptureStopped;
            try { old.StopRecording(); } catch { }
            old.Dispose();
        }
        if (oldPlayback != null)
        {
            oldPlayback.PlaybackStopped -= MonitorPlaybackStopped;
            try { oldPlayback.Stop(); } catch { }
            oldPlayback.Dispose();
        }
        monitorBuffer = null; monitorGain = null;
        if (captureDevice != null) { captureDevice.Dispose(); captureDevice = null; }
        if (playbackDevice != null) { playbackDevice.Dispose(); playbackDevice = null; }
        monitor.Text = "Start monitoring"; input.Enabled = output.Enabled = true;
    }

    void StopSample()
    {
        var old = samplePlayer; samplePlayer = null;
        if (old != null)
        {
            old.PlaybackStopped -= SampleStopped;
            try { old.Stop(); } catch { }
            old.Dispose();
        }
        if (sampleFile != null) { sampleFile.Dispose(); sampleFile = null; }
        if (sampleDevice != null) { sampleDevice.Dispose(); sampleDevice = null; }
        send.Text = "Send WAV to synth...";
    }

    void SampleStopped(object sender, StoppedEventArgs e)
    {
        QueueForCurrentStream(sender, delegate { return samplePlayer; }, delegate {
            StopSample(); status.Text = e.Exception == null ?
                "WAV transfer finished. Tap PLAY SAMPLE on the board." : "Sample transfer: " + e.Exception.Message;
        });
    }

    void SendSample()
    {
        if (samplePlayer != null) { StopSample(); return; }
        using (var dialog = new OpenFileDialog { Filter = "WAV audio (*.wav)|*.wav", Title = "Send audio to STM32" })
        {
            if (dialog.ShowDialog(this) != DialogResult.OK) return;
            try
            {
                sampleDevice = devices.EnumerateAudioEndPoints(DataFlow.Render, DeviceState.Active)
                    .FirstOrDefault(d => d.FriendlyName.IndexOf("Synth", StringComparison.OrdinalIgnoreCase) >= 0);
                if (sampleDevice == null) { status.Text = "Synth playback endpoint is unavailable."; return; }
                if (MessageBox.Show(this, "Tap RECORD on the board, then click OK to send the WAV audio. The board stores the first half-second.",
                    "Ready to sample", MessageBoxButtons.OKCancel) != DialogResult.OK) { StopSample(); return; }
                sampleFile = new WaveFileReader(dialog.FileName);
                samplePlayer = new WasapiOut(sampleDevice, AudioClientShareMode.Shared, true, 50);
                samplePlayer.Init(sampleFile); samplePlayer.PlaybackStopped += SampleStopped; samplePlayer.Play();
                send.Text = "Stop sending"; status.Text = "Sending WAV audio to the board's USB sample input.";
            }
            catch (Exception ex) { StopSample(); status.Text = "Cannot send WAV: " + ex.Message; }
        }
    }

    static int GainSelfTest()
    {
        // Exercise the monitor's PCM-to-float gain chain without opening any audio endpoint.
        byte[] pcm = { 0, 64, 0, 192, 255, 127, 0, 128 };
        using (var stream = new RawSourceWaveStream(new MemoryStream(pcm), new WaveFormat(48000, 16, 1)))
        {
            var gain = new SampleChannel(stream, false) { Volume = 0.25f };
            var provider = new SampleToWaveProvider(gain);
            byte[] result = new byte[8];
            if (provider.Read(result, 0, result.Length) != result.Length ||
                Math.Abs(BitConverter.ToSingle(result, 0) - 0.125f) > 0.000001f ||
                Math.Abs(BitConverter.ToSingle(result, 4) + 0.125f) > 0.000001f) return 1;
            gain.Volume = 0.5f;
            if (provider.Read(result, 0, result.Length) != result.Length ||
                Math.Abs(BitConverter.ToSingle(result, 0) - 32767f / 65536f) > 0.000001f ||
                Math.Abs(BitConverter.ToSingle(result, 4) + 0.5f) > 0.000001f) return 1;
            return provider.Read(result, 0, result.Length) == 0 ? 0 : 1;
        }
    }

    [STAThread]
    static int Main(string[] args)
    {
        if (args.Length > 0 && args[0] == "--gain-self-test") return GainSelfTest();
        if (args.Length > 0 && args[0] == "--self-test")
        {
            try
            {
                using (var enumerator = new MMDeviceEnumerator())
                {
                    var captures = enumerator.EnumerateAudioEndPoints(DataFlow.Capture, DeviceState.Active);
                    var outputs = enumerator.EnumerateAudioEndPoints(DataFlow.Render, DeviceState.Active);
                    File.WriteAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "self-test.txt"),
                        "WASAPI initialized\nCapture devices: " + captures.Count + "\nPlayback devices: " + outputs.Count);
                }
                return 0;
            }
            catch (Exception ex) { File.WriteAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "self-test.txt"), ex.ToString()); return 1; }
        }
        Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
        Application.Run(new SynthMonitor()); return 0;
    }
}
