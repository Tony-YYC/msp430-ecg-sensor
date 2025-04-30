use eframe::egui;
use egui_plot as eplot;  // 导入单独的绘图模块
use rustfft::{FftPlanner, num_complex::Complex};

// 信号生成器类
struct SignalGenerator {
    samples: Vec<f32>,
    sample_rate: f32,
}

impl SignalGenerator {
    // 创建一个新的信号生成器，生成正弦波
    fn new(sample_count: usize, sample_rate: f32) -> Self {
        let mut samples = vec![0.0; sample_count];
        
        // 生成一个包含三个不同频率分量的复合正弦波
        // 主分量: 2 Hz
        // 次分量: 10 Hz, 25 Hz
        for i in 0..sample_count {
            let t = i as f32 / sample_rate;
            samples[i] = 
                1.0 * (2.0 * std::f32::consts::PI * 2.0 * t).sin() +  // 2 Hz
                0.5 * (2.0 * std::f32::consts::PI * 10.0 * t).sin() + // 10 Hz
                0.25 * (2.0 * std::f32::consts::PI * 25.0 * t).sin(); // 25 Hz
        }
        
        Self { samples, sample_rate }
    }
    
    // 获取生成的样本
    fn get_samples(&self) -> &[f32] {
        &self.samples
    }
}

// 信号处理器类
struct SignalProcessor {
    fft_planner: FftPlanner<f32>,
}

impl SignalProcessor {
    fn new() -> Self {
        Self {
            fft_planner: FftPlanner::new(),
        }
    }
    
    // 执行FFT分析
    fn compute_fft(&mut self, samples: &[f32]) -> (Vec<f32>, Vec<f32>) {
        let n = samples.len();
        
        // 转换为复数
        let mut complex_samples: Vec<Complex<f32>> = samples
            .iter()
            .map(|&x| Complex::new(x, 0.0))
            .collect();
            
        // 创建并执行FFT
        let fft = self.fft_planner.plan_fft_forward(n);
        fft.process(&mut complex_samples);
        
        // 计算频率和幅度
        let magnitudes: Vec<f32> = complex_samples
            .iter()
            .map(|c| (c.norm() / (n as f32).sqrt()))
            .collect();
            
        let frequencies: Vec<f32> = (0..n)
            .map(|i| if i <= n/2 { i as f32 } else { i as f32 - n as f32 })
            .collect();
            
        (frequencies, magnitudes)
    }
    
    // 找出频谱中最大的三个峰值
    fn find_top_peaks(&self, frequencies: &[f32], magnitudes: &[f32], sample_rate: f32) -> Vec<(f32, f32)> {
        let n = frequencies.len();
        let mut peaks = Vec::new();
        
        // 只考虑频谱的前半部分（因为FFT结果是对称的）
        let half_n = n / 2;
        
        // 创建频率、幅度对
        let mut freq_mag_pairs: Vec<(f32, f32)> = frequencies[1..half_n]
            .iter()
            .zip(magnitudes[1..half_n].iter())
            .map(|(&f, &m)| (f * sample_rate / n as f32, m))
            .collect();
            
        // 按幅度排序
        freq_mag_pairs.sort_by(|a, b| b.1.partial_cmp(&a.1).unwrap());
        
        // 取前三个
        peaks = freq_mag_pairs.into_iter().take(3).collect();
        
        peaks
    }
}

// GUI应用类
struct EcgApp {
    signal_generator: SignalGenerator,
    signal_processor: SignalProcessor,
    fft_data: Option<(Vec<f32>, Vec<f32>)>,
    top_peaks: Vec<(f32, f32)>,
    sample_rate: f32,
    show_fft: bool,
    show_raw_data: bool,
}

impl EcgApp {
    fn new(cc: &eframe::CreationContext<'_>) -> Self {
        let sample_count = 500;
        let sample_rate = 500.0; // 500 Hz
        let signal_generator = SignalGenerator::new(sample_count, sample_rate);
        let mut signal_processor = SignalProcessor::new();
        
        let samples = signal_generator.get_samples();
        let fft_data = Some(signal_processor.compute_fft(samples));
        
        let top_peaks = if let Some((frequencies, magnitudes)) = &fft_data {
            signal_processor.find_top_peaks(frequencies, magnitudes, sample_rate)
        } else {
            Vec::new()
        };
        
        Self {
            signal_generator,
            signal_processor,
            fft_data,
            top_peaks,
            sample_rate,
            show_fft: true,
            show_raw_data: true,
        }
    }
}

impl eframe::App for EcgApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        egui::CentralPanel::default().show(ctx, |ui| {
            ui.heading("ECG Signal Visualization Demo");
            
            ui.checkbox(&mut self.show_raw_data, "Show Original Waveform");
            ui.checkbox(&mut self.show_fft, "Show FFT Spectrum");
            
            ui.separator();
            
            if self.show_raw_data {
                ui.heading("Waveform");
                self.plot_waveform(ui);
            }
            
            if self.show_fft {
                ui.heading("FFT Spectrum");
                self.plot_fft(ui);
                
                ui.separator();
                ui.heading("Top3 Frequency Peak");
                for (i, (freq, mag)) in self.top_peaks.iter().enumerate() {
                    ui.label(format!("#{}: {:.2} Hz, Amplitude: {:.4}", i+1, freq, mag));
                }
            }
        });
    }
}

impl EcgApp {
    // 绘制波形图
    fn plot_waveform(&self, ui: &mut egui::Ui) {
        let samples = self.signal_generator.get_samples();
        
        let plot = eplot::Plot::new("waveform_plot")
            .height(200.0)
            .view_aspect(3.0);
            
        plot.show(ui, |plot_ui| {
            let points: Vec<[f64; 2]> = samples
                .iter()
                .enumerate()
                .map(|(i, &y)| [i as f64 / self.sample_rate as f64, y as f64])
                .collect();
                
            let line = eplot::Line::new(eplot::PlotPoints::new(points))
                .color(egui::Color32::from_rgb(0, 150, 255))
                .name("ECG Signal");
                
            plot_ui.line(line);
        });
    }
    
    // 绘制FFT频谱
    fn plot_fft(&self, ui: &mut egui::Ui) {
        if let Some((frequencies, magnitudes)) = &self.fft_data {
            let plot = eplot::Plot::new("fft_plot")
                .height(200.0)
                .view_aspect(3.0);
                
            plot.show(ui, |plot_ui| {
                // 只显示频谱的前半部分（有效频率）
                let half_n = frequencies.len() / 2;
                
                let points: Vec<[f64; 2]> = frequencies[0..half_n]
                    .iter()
                    .zip(magnitudes[0..half_n].iter())
                    .map(|(&f, &m)| [f as f64 * self.sample_rate as f64 / frequencies.len() as f64, m as f64])
                    .collect();
                    
                let line = eplot::Line::new(eplot::PlotPoints::new(points))
                    .color(egui::Color32::from_rgb(255, 100, 100))
                    .name("FFT Spectrum");
                    
                plot_ui.line(line);
                
                // 标记峰值
                for (freq, mag) in &self.top_peaks {
                    let point = eplot::Points::new(vec![[*freq as f64, *mag as f64]])
                        .color(egui::Color32::from_rgb(50, 220, 50))
                        .radius(5.0)
                        .name(format!("{:.2} Hz", freq));
                        
                    plot_ui.points(point);
                }
            });
        }
    }
}

fn main() -> Result<(), eframe::Error> {
    let options = eframe::NativeOptions {
        initial_window_size: Some(egui::vec2(800.0, 600.0)),
        ..Default::default()
    };
    
    eframe::run_native(
        "ECG Signal Visualization Demo",
        options,
        Box::new(|cc| Box::new(EcgApp::new(cc)))
    )
}