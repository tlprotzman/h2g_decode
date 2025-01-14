#include <TCanvas.h>
#include <TF1.h>
#include <TFile.h>
#include <TGraph.h>
#include <TH1F.h>
#include <TH2F.h>
#include <TH3F.h>
#include <TLatex.h>
#include <TROOT.h>
#include <TStyle.h>
#include <TTree.h>
#include <TError.h>
#include <TProfile.h>
#include <TLegend.h>
#include <TFitResult.h>
#include <TFitResultPtr.h>

#include <iostream>
#include <vector>

#include <csignal>

volatile sig_atomic_t gSignalStatus = 0;

void signal_handler(int signal) {
    gSignalStatus = signal;
}

void setup_signal_handler() {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
}


// Crystal Ball with exponential tail to the right
double crystal_ball(double *inputs, double *par) {
    // Parameters
    // alpha: Where the gaussian transitions to the power law tail - fix?
    // n: The exponent of the power law tail - fix?
    // x_bar: The mean of the gaussian - free
    // sigma: The width of the gaussian - fix ?
    // N: The normalization of the gaussian - free
    // B baseline - fix?

    double x = inputs[0];

    double alpha = par[0];
    double n = par[1];
    double x_bar = par[2];
    double sigma = par[3];
    double N = par[4];
    double offset = par[5];
    
    double A = pow(n / fabs(alpha), n) * exp(-0.5 * alpha * alpha);
    double B = n / fabs(alpha) - fabs(alpha);
    // std::cout << "A: " << A << std::endl;

    // std::cout << "alpha: " << alpha << " n: " << n << " x_bar: " << x_bar << " sigma: " << sigma << " N: " << N << " B: " << B << " A: " << A << std::endl;

    double ret_val;
    if ((x - x_bar) / sigma < alpha) {
        // std::cout << "path a" << std::endl;
        ret_val = exp(-0.5 * (x - x_bar) * (x - x_bar) / (sigma * sigma));
    } else {
        // std::cout << "path b" << std::endl;
        ret_val = A * pow(B + (x - x_bar) / sigma, -1 * n);
    }
    ret_val = N * ret_val + offset;
    // std::cout << "x: " << x << " y: " << ret_val << std::endl;
    return ret_val;
}

// Exponentially modified gaussian
double exp_mod_gaussian(double *x, double *par) {
    // Parameters
    // mu: mean of the gaussian
    // sigma: width of the gaussian
    // lambda: rate of the exponential decay
    double mu = par[0];
    double sigma = par[1];
    double lambda = par[2];
    double offset = par[3];

    double z = (x[0] - mu) / sigma;
    double exp_term = exp((lambda / 2) * (2 * mu + lambda * sigma * sigma - 2 * x[0]));
    double erf_term = erfc((mu + lambda * sigma * sigma - x[0]) / (sqrt(2) * sigma));

    return offset + (lambda / 2) * exp_term * erf_term;
}

int identify_muon_track_a(float column_sum[8], float column_N[8][64]) {
    // Find the column with the most "energy"
    int max_column = 0;
    float max_value = 0;
    for (int i = 0; i < 8; i++) {
        if (column_sum[i] > max_value) {
            max_value = column_sum[i];
            max_column = i;
        }
    }

    // Check if the column with the most energy is significantly higher than the others
    int num_below_threshold = 0;
    for (int i = 0; i < 8; i++) {
        if (i == max_column) {
            continue;
        }
        if (column_sum[i] / max_value < 0.3) {  // Count how many are below 20% of the max
            num_below_threshold++;
        }
    }
    // If more than 6 other columns aren't below the threshold, it's probably not a muon
    if (num_below_threshold <= 6) {
        return -1;
    }
    
    // Alright, so we think we have a muon.  It could be noise, let's count how many in a column are below 10
    int num_below_threshold_in_column = 0;
    for (int i = 0; i < 64; i++) {
        if (column_N[max_column][i] < 10) {
            num_below_threshold_in_column++;
        }
    }
    // If more than 30% of the channels in the column are below 10, it's probably not a muon
    if (num_below_threshold_in_column > 20) {
        return -1;
    }
    return 1 << max_column;
}

int identify_muon_track_b(float column_sum[8], float column_N[8][64]) {
    int ret = 0b00000000;
    for (int i = 0; i < 8; i++) {
        int above_threshold = 0;
        for (int j = 0; j < 64; j++) {
            if (column_N[i][j] > 20) {
                above_threshold++;
            }
        }
        if (above_threshold > 4) {
            ret |= 1 << i;
        }
    }
    return ret;
}

int identify_muon_track(float column_sum[8], float column_N[8][64]) {
    return identify_muon_track_b(column_sum, column_N);
}


void draw_waveforms(int run_number) {
    setup_signal_handler();
    // ROOT::EnableImplicitMT();
    gStyle->SetOptStat(0);
    gErrorIgnoreLevel = kWarning;

    // Open the ROOT file
    TString file_name = Form("/Users/tristan/epic/hgcroc/h2g_decode/output/run%d.root", run_number);
    TFile *file = TFile::Open(file_name);
    if (!file || file->IsZombie()) {
        std::cerr << "Error opening file" << std::endl;
        return;
    }

    // Get the tree from the file
    TTree *tree;
    file->GetObject("events", tree);
    if (!tree) {
        std::cerr << "Error getting tree from file" << std::endl;
        return;
    }

    // Set up branches
    uint waveform[576][10];
    uint channel_x[576];
    uint channel_y[576];
    uint channel_z[576];
    bool good_channel[576];
    tree->SetBranchAddress("adc", &waveform);
    tree->SetBranchAddress("hit_x", &channel_x);
    tree->SetBranchAddress("hit_y", &channel_y);
    tree->SetBranchAddress("hit_z", &channel_z);
    tree->SetBranchAddress("good_channel", &good_channel);

    // Create a canvas to draw the waveforms
    TCanvas *canvas = new TCanvas("canvas", "Waveforms", 800, 600);

    // Make a histogram for each channel
    std::vector<TH2F*> per_channel_waveform;
    for (int i = 0; i < 576; i++) {
        auto histo = new TH2F(Form("waveform_%d", i), Form("Waveform %d", i), 10, 0, 10, 1024, 0, 1024);
        histo->GetXaxis()->SetTitle("Sample");
        histo->GetYaxis()->SetTitle("ADC");
        histo->SetMaximum(1024);
        histo->SetMinimum(0);
        per_channel_waveform.push_back(histo);
    }

    // Track the height of the landau curve
    std::vector<TH1F*> fit_magnitude;
    std::vector<TH1F*> muon_track_fit_magnitude;
    std::vector<TH1F*> fit_integral;
    for (int i = 0; i < 576; i++) {
        auto histo = new TH1F(Form("cb_norm_%d", i), Form("Fit Magnitude %d", i), 250, 0, 1000);
        histo->GetXaxis()->SetTitle("Normalization factor");
        histo->GetYaxis()->SetTitle("Counts");
        fit_magnitude.push_back(histo);

        histo = new TH1F(Form("muon_cb_norm_%d", i), Form("Muon Track Fit Magnitude %d", i), 250, 0, 1000);
        histo->GetXaxis()->SetTitle("Normalization factor");
        histo->GetYaxis()->SetTitle("Counts");
        muon_track_fit_magnitude.push_back(histo);

        histo = new TH1F(Form("cb_i_%d", i), Form("Fit Integral %d", i), 250, 0, 1000);
        histo->GetXaxis()->SetTitle("Integral");
        histo->GetYaxis()->SetTitle("Counts");
        fit_integral.push_back(histo);
    }

    // Event display histogram
    auto *event_display = new TH3F("event_display", "Event Display;X;Z;Y", 4, 0, 4, 64, 0, 64, 2, 0, 2);

    // Longitudinal profile
    auto *longitudinal_profile = new TH2F("longitudinal_profile", "Longitudinal Profile;Z;N", 64, 0, 64, 250, 0, 2000);

    // Loop over entries in the tree
    int nEntries = tree->GetEntries();
    std::cout << "Number of entries: " << nEntries << std::endl;
    // Dummy histo to set the axis
    TH1F *histo = new TH1F("histo", "Waveform", 10, 0, 9.5);
    histo->GetXaxis()->SetTitle("Sample");
    histo->GetYaxis()->SetTitle("ADC");
    histo->SetMaximum(160);
    histo->SetMinimum(65);


    // Initial fit parameters
    double par[7];
    // mod_gauss
    // par[0]=2.5;
    // par[1]=1;
    // par[2]=1;
    // par[3]=100;
    
    // Crystal ball
    par[0] = 2; // alpha
    par[1] = 0.2; // n
    par[2] = 2; // x_bar
    par[3] = 0.25; // sigma
    par[4] = 0; // N
    par[5] = 100; // offset

    // Histograms of the fit parameters
    int bins = 50;
    int channels = 576;
    TH3F *alpha_hist = new TH3F("alpha_hist", "Alpha;Channel;Value", channels, 0, channels, bins, 0, 5, 250, 0, 1000);
    TH3F *n_hist = new TH3F("n_hist", "n;Channel;Value", channels, 0, channels, bins, 0, 4, 250, 0, 1000);
    TH3F *x_bar_hist = new TH3F("x_bar_hist", "x_bar;Channel;Value", channels, 0, channels, bins, 0, 5, 250, 0, 1000);
    TH3F *sigma_hist = new TH3F("sigma_hist", "sigma;Channel;Value", channels, 0, channels, bins, 0.0, 3, 250, 0, 1000);
    TH3F *N_hist = new TH3F("N_hist", "N;Channel;Value", channels, 0, channels, bins, 0, 2000, 250, 0, 1000);
    TH3F *offset_hist = new TH3F("offset_hist", "offset;Channel;Value", channels, 0, channels, bins, 0, 150, 250, 0, 1000);

    // attempt to open the pedestal file
    TFile *pedestal_file = TFile::Open(Form("pedestals_run%d.root", run_number));
    TH1F *pedestals = nullptr;
    if (pedestal_file) {
        pedestal_file->GetObject("pedestals", pedestals);
    }

    // maximums 
    float max_n = 0;
    float max_int = 0;

    int muons_searched_for = 0;
    int muons_identified = 0;


    // Save as multi page pdf
    canvas->Print(Form("waveforms_run%d.pdf[", run_number));
    for (int i = 0; i < nEntries; ++i) {
        if (gSignalStatus != 0) {
            std::cerr << "Caught signal " << gSignalStatus << ", exiting" << std::endl;
            break;
        }
        if (i % 1 == 0) {
            std::cout << "\rProcessing entry " << i << std::flush;
        }
        tree->GetEntry(i);
        if (i > 30000) {    // I don't know why but the program crashes at entry 31401
            break;
        }
       
        // To identify muon tracks, we'll take the sum of N for each column
        float column_sum[8];
        int column_index[8][64];
        float column_N[8][64];
        for (int i = 0; i < 8; i++) {
            column_sum[i] = 0;
            for (int j = 0; j < 64; j++) {
                column_index[i][j] = -1;
                column_N[i][j] = 0;
            }
        }

        event_display->Reset();
        for (int channel = 0; channel < 576; channel++) {
            if (!good_channel[channel]) {
                continue;
            }
            auto graph = new TGraph(10);
            for (int j = 0; j < 10; j++) {
                graph->SetPoint(j, j, waveform[channel][j]);
                per_channel_waveform[channel]->Fill(j, waveform[channel][j]);
            }
            // Try fitting with a constant first
            auto *constant_fit = new TF1("constant", "[0]", 0, 9);
            if (pedestals) {
                constant_fit->SetParameter(0, pedestals->GetBinContent(channel));
                constant_fit->SetParLimits(0, pedestals->GetBinContent(channel) * 0.9, pedestals->GetBinContent(channel) * 1.1);
            } else {
                constant_fit->SetParameter(0, 70);
                constant_fit->SetParLimits(0, 63, 77);
            }
            auto fit_results = graph->Fit(constant_fit, "Q0", "");
            // if this fits well, there probably isn't a signal
            double constant_chi2 = constant_fit->GetChisquare();// / constant_fit->GetNDF();


            // Fit the graph with a landau curve
            // TF1 *fit = new TF1("fit", "landau", 0, 10);
            // fit->SetParameter(1, 3);
            TF1 *fit = new TF1("fit", crystal_ball, 0, 9.5, 6);
            // TF1 *fit = new TF1("fit", exp_mod_gaussian, 0, 10, 4);
            fit->SetParameters(par);
            // fit->FixParameter(0, 2);
            // fit->FixParameter(1, 500);
            // fit->FixParameter(3, 1);
            fit->SetParLimits(0, 1, 1.2);   // alpha
            // fit->SetParLimits(0, 0, 5);   // alpha
            // fit->FixParameter(0, 1.1);   // alpha
            // fit->FixParameter(1, 0.43);   // alpha
            fit->SetParLimits(1, 0.2, 0.6);   // n
            fit->SetParLimits(2, 0.5, 4.5);   // x_bar
            fit->SetParLimits(3, 0.25, 0.65);   // sigma
            // fit->FixParameter(3, 0.25);
            fit->SetParLimits(4, 0, 2000);  // N
            // Set offset
            if (true && pedestals) {
                fit->FixParameter(5, pedestals->GetBinContent(channel));
            } else {
                fit->FixParameter(5, (waveform[channel][0] + waveform[channel][9]) / 2);
            }
            double N = 0;
            if (constant_chi2 > 500) {
                graph->Fit(fit, "Q0", "", 0, 9);
                N = fit->GetParameter(4);
                // if (N > 200) {
                    alpha_hist->Fill(channel, fit->GetParameter(0), N);
                    n_hist->Fill(channel, fit->GetParameter(1), N);
                    x_bar_hist->Fill(channel, fit->GetParameter(2), N);
                    sigma_hist->Fill(channel, fit->GetParameter(3), N);
                    N_hist->Fill(channel, fit->GetParameter(4), N);
                    offset_hist->Fill(channel, fit->GetParameter(5), N);
                // }
            }
            double crystal_ball_chi2 = fit->GetChisquare() / fit->GetNDF();
            // Save the fit parameters

            // auto mpv = fit->GetParameter(1);

            // Track which column has the largest signal to identify muon tracks
            int index = channel_x[channel] + 4 * channel_y[channel];
            column_sum[index] += N;
            column_index[index][channel_z[channel]] = channel;
            column_N[index][channel_z[channel]] = N;

            fit_magnitude[channel]->Fill(N);
            longitudinal_profile->Fill(channel_z[channel], N);
            event_display->Fill(channel_x[channel], channel_z[channel], channel_y[channel], N);
            
            if (N > max_n) {
                max_n = N;
            }

            // Calculate integral above the pedestal
            // auto integral = fit->Integral(0, 9);
            // integral -= fit->GetParameter(5) * 10;
            // fit_integral[channel]->Fill(integral);
            // if (integral > max_int) {
            //     max_int = integral;
            // }
            double integral = 0;
            
            int draw = 0;
            if (draw) {
                // if (channel != 61) {
                //     continue;
                // }
                canvas->SetTopMargin(0.05);
                canvas->SetRightMargin(0.05);
                histo->SetTitle(Form("Event %d Channel %d", i, channel));
                histo->SetTitle("");
                histo->Draw();
                histo->GetXaxis()->SetTitleSize(0.05);
                histo->GetXaxis()->SetTitleOffset(0.795);
                histo->GetYaxis()->SetTitleSize(0.05);
                histo->GetYaxis()->SetTitleOffset(0.81);
                graph->SetMarkerStyle(20);
                graph->Draw("same p");
                fit->SetFillColorAlpha(kAzure+6, 0.15);
                fit->SetFillStyle(1001);
                fit->SetLineColor(kBlue);
                fit->Draw("same E3");
                auto fit_copy = (TF1*)fit->Clone();
                fit_copy->SetFillStyle(0);
                fit_copy->DrawCopy("same");
                // constant_fit->SetLineColor(kBlue);
                // constant_fit->Draw("same");
                TLatex latex;
                latex.SetNDC();
                // latex.DrawLatex(0.15, 0.85, Form("alpha = %.f", fit->GetParameter(0)));
                // latex.DrawLatex(0.15, 0.80, Form("n = %.2f", fit->GetParameter(1)));
                // latex.DrawLatex(0.15, 0.75, Form("x_bar = %.2f", fit->GetParameter(2)));
                // latex.DrawLatex(0.15, 0.70, Form("sigma = %.2f", fit->GetParameter(3)));
                // latex.DrawLatex(0.15, 0.65, Form("offset = %.2f", fit->GetParameter(4)));
                latex.SetTextSize(0.04);
                latex.SetTextFont();
                latex.SetTextAlign(11);
                latex.DrawLatex(0.15, 0.9, "LFHCal Test Beam");
                latex.SetTextFont(42);
                latex.SetTextSize(0.035);
                latex.DrawLatex(0.15, 0.86, "CERN PS, 5 GeV#mu^{+}");
                latex.DrawLatex(0.15, 0.82, Form("Run %d Event %d Channel %d", run_number, i, channel));
                // latex.DrawLatex(0.15, 0.78, Form("Coordinate: (%d, %d, %d)", channel_x[channel], channel_y[channel], channel_z[channel]));
                latex.DrawLatex(0.15, 0.78, Form("V_{op} = 43 V"));

                latex.SetTextAlign(11);
                latex.SetTextFont(62);
                latex.DrawLatex(0.65, 0.9, "Crystal Ball Fit Parameters");
                latex.SetTextFont(42);
                latex.DrawLatex(0.65, 0.86, Form("#alpha = %.2f", fit->GetParameter(0)));
                latex.DrawLatex(0.65, 0.82, Form("n = %.2f", fit->GetParameter(1)));
                latex.DrawLatex(0.65, 0.78, Form("#bar{x} = %.2f", fit->GetParameter(2)));
                latex.DrawLatex(0.65, 0.74, Form("#sigma = %.2f", fit->GetParameter(3)));
                latex.DrawLatex(0.65, 0.70, Form("N = %.2f", fit->GetParameter(4)));
                latex.DrawLatex(0.65, 0.66, Form("offset = %.2f", fit->GetParameter(5)));
                // latex.DrawLatex(0.65, 0.67, Form("constant chi^2 = %.2f", constant_chi2));
                latex.DrawLatex(0.65, 0.62, Form("#chi^{2}/NDF = %.2f", crystal_ball_chi2));
                canvas->Update();
                if (N > draw && crystal_ball_chi2 < 3) {
                    canvas->Print(Form("waveforms_run%d.pdf", run_number));
                    // std::cout << "saving waveform" << std::endl;
                    canvas->Print(Form("candidate_waveforms/waveform_run%d_event%d_channel%d.png", run_number, i, channel));
                }
            }
            delete graph;
    
        }
        // identify if we have a muon track
        int muon_tracks = identify_muon_track(column_sum, column_N);
        muons_searched_for++;
        if (muon_tracks) {
            muons_identified++;
        }
        for (int i = 0; i < 8; i++) {
            if (muon_tracks & (1 << i)) {
                for (int j = 0; j < 64; j++) {
                    muon_track_fit_magnitude[column_index[i][j]]->Fill(column_N[i][j]);
                }
            }
        }
    
        canvas->Clear();
        // canvas->Divide(2, 2);
        if (muon_tracks) {
            event_display->SetTitle(Form("Run %d Event %d: Muon in column %d", run_number, i, muon_tracks));
        } else {
            event_display->SetTitle(Form("Run %d Event %d", run_number, i));
        }
        
        // canvas->cd(1);
        // event_display->SetTitle("");
        event_display->Draw("box2");
        
        TLatex latex;
        latex.SetNDC();
        latex.SetTextSize(0.035);
        latex.SetTextAlign(11);
        latex.SetTextFont(62);
        latex.DrawLatex(0.1, 0.92, "LFHCal Test Beam");
        latex.SetTextFont(42);
        latex.DrawLatex(0.1, 0.88, "CERN PS, 5 GeV #mu^{+}");
        latex.DrawLatex(0.1, 0.84, Form("Run %d Event %d", run_number, i));

        event_display->GetXaxis()->SetTitleOffset(1.5);
        event_display->GetYaxis()->SetTitleOffset(2);
        event_display->GetZaxis()->SetTitleOffset(1.2);
        // auto event_display_copy = (TH3F*)event_display->Clone();
        
        // canvas->cd(2);
        // auto tmp = event_display_copy->Project3D("zx");
        // tmp->SetTitle("XY View");
        // tmp->Draw("colz");
        
        // canvas->cd(3);
        // tmp = event_display_copy->Project3D("zy");
        // tmp->SetTitle("ZY View");
        // tmp->Draw("colz");
        
        // canvas->cd(4);
        // tmp = event_display_copy->Project3D("xy");
        // tmp->SetTitle("ZX View");
        // tmp->Draw("colz");

        // canvas->Update();
        canvas->Print(Form("waveforms_run%d.pdf", run_number));
        canvas->Print(Form("candidate_event_display/event_display_run%d_event%d.png", run_number, i));
        // canvas->Clear();
        // canvas->Divide(1, 1);
    }
    canvas->Print(Form("waveforms_run%d.pdf]", run_number));
    // canvas->Print(Form("event_display_run%d.pdf]", run_number));
    std::cout << "Max N: " << max_n << std::endl;
    std::cout << "Max Integral: " << max_int << std::endl;
    std::cout << "Muons searched for: " << muons_searched_for << std::endl;
    std::cout << "Muons identified: " << muons_identified << std::endl;

    if (pedestal_file) {
        pedestal_file->Close();
    }

    // Draw all waveforms
    canvas->Print(Form("waveforms_all_run%d.pdf[", run_number));
    for (int i = 0; i < 576; i++) {
        per_channel_waveform[i]->Draw("colz");
        canvas->Update();
        canvas->Print(Form("waveforms_all_run%d.pdf", run_number));
    }
    canvas->Print(Form("waveforms_all_run%d.pdf]", run_number));

    auto mip_fit = new TF1("mip_fit", "landau + [3] * exp(-[4] * x)", 0, 600);
    // mip_fit->SetParLimits(0, 0, 75);    // landau amplitude
    mip_fit->SetParLimits(1, 100, 400);   // landau mpv
    mip_fit->SetParLimits(2, 0.4, 10000);  // landau width
    mip_fit->SetParLimits(3, 0, 10000);   // exponential amplitude
    mip_fit->SetParLimits(4, 0, 1);   // exponential decay constant
    // Draw all landau heights
    canvas->Print(Form("fit_magnitude_run%d.pdf[", run_number));
    for (int i = 0; i < 576; i++) {
        if (!good_channel[i]) {
            continue;
        }
        fit_magnitude[i]->Fit(mip_fit, "QR", "");
        fit_magnitude[i]->Draw("e");
        fit_integral[i]->SetLineColor(kRed);
        fit_integral[i]->Draw("e same");

        TLegend *legend = new TLegend(0.6, 0.6, 0.9, 0.9);
        legend->AddEntry(fit_magnitude[i], "Crystal Ball Normalization", "l");
        legend->AddEntry(fit_integral[i], "Crystal Ball Integral", "l");
        legend->Draw();

        gPad->SetLogy();
        TLatex latex;
        latex.SetNDC();
        latex.SetTextSize(0.03);
        latex.SetTextAlign(31);
        latex.DrawLatex(0.55, 0.90, Form("Landau Amplitude = %.2f", mip_fit->GetParameter(0)));
        latex.DrawLatex(0.55, 0.85, Form("Landau MPV = %.2f", mip_fit->GetParameter(1)));
        latex.DrawLatex(0.55, 0.80, Form("Landau Width = %.2f", mip_fit->GetParameter(2)));
        latex.DrawLatex(0.55, 0.75, Form("Exp Amplitude = %.2f", mip_fit->GetParameter(3)));
        latex.DrawLatex(0.55, 0.70, Form("Exp Decay = %.2f", mip_fit->GetParameter(4)));

        latex.DrawLatex(0.15, 0.15, Form("Coordinate: (%d, %d, %d)", channel_x[i], channel_y[i], channel_z[i]));
        
        
        
        canvas->Update();
        canvas->Print(Form("fit_magnitude_run%d.pdf", run_number));
    }
    canvas->Print(Form("fit_magnitude_run%d.pdf]", run_number));

    // Save fit parameter histograms to a PDF
    canvas->Clear();
    gPad->SetLogy(0);   // not sure if I need this
    canvas->Divide(3, 2);
    canvas->Print(Form("fit_parameters_run%d.pdf[", run_number));
    TLatex latex;
    latex.SetTextSize(0.05);
    // When using TH2
    // for (int i = 0; i < 576; i++) {
    //     // Label the channel
    //     canvas->cd(1);
    //     alpha_hist->ProjectionY(Form("alpha_hist_proj_%d", i), i, i)->Draw("colz");
    //     latex.DrawLatexNDC(0.2, 0.85, Form("Channel %d", i));
    //     // canvas->Print(Form("fit_parameters_run%d.pdf", run_number));
    //     canvas->cd(2);
    //     n_hist->ProjectionY(Form("n_hist_proj_%d", i), i, i)->Draw("colz");
    //     // canvas->Print(Form("fit_parameters_run%d.pdf", run_number));
    //     canvas->cd(3);
    //     x_bar_hist->ProjectionY(Form("x_bar_hist_proj_%d", i), i, i)->Draw("colz");
    //     // canvas->Print(Form("fit_parameters_run%d.pdf", run_number));
    //     canvas->cd(4);
    //     sigma_hist->ProjectionY(Form("sigma_hist_proj_%d", i), i, i)->Draw("colz");
    //     // canvas->Print(Form("fit_parameters_run%d.pdf", run_number));
    //     canvas->cd(5);
    //     N_hist->ProjectionY(Form("N_hist_proj_%d", i), i, i)->Draw("colz");
    //     // canvas->Print(Form("fit_parameters_run%d.pdf", run_number));
    //     canvas->cd(6);
    //     offset_hist->ProjectionY(Form("offset_hist_proj_%d", i), i, i)->Draw("colz");
    //     canvas->Print(Form("fit_parameters_run%d.pdf", run_number));

    // }
    // When using TH3
    for (int i = 0; i < 577; i++) {
        // Label the channel
        canvas->cd(1);
        alpha_hist->GetXaxis()->SetRange(i, i);
        auto alpha_hist_proj = alpha_hist->Project3D("yz");
        alpha_hist_proj->SetTitle(Form("alpha_hist_proj_%d", i));
        alpha_hist_proj->Draw("colz");
        gPad->SetLogz();
        if (i == 0) {
            latex.DrawLatexNDC(0.2, 0.85, Form("All channels %d", i));
        } else {
            latex.DrawLatexNDC(0.2, 0.85, Form("Channel %d", i - 1));
        }
        // canvas->Print(Form("fit_parameters_run%d.pdf", run_number));
        canvas->cd(2);
        n_hist->GetXaxis()->SetRange(i, i);
        auto n_hist_proj = n_hist->Project3D("yz");
        n_hist_proj->SetTitle(Form("n_hist_proj_%d", i));
        n_hist_proj->Draw("colz");
        gPad->SetLogz();
        // canvas->Print(Form("fit_parameters_run%d.pdf", run_number));
        canvas->cd(3);
        x_bar_hist->GetXaxis()->SetRange(i, i);
        auto x_bar_hist_proj = x_bar_hist->Project3D("yz");
        x_bar_hist_proj->SetTitle(Form("x_bar_hist_proj_%d", i));
        x_bar_hist_proj->Draw("colz");
        gPad->SetLogz();
        // canvas->Print(Form("fit_parameters_run%d.pdf", run_number));
        canvas->cd(4);
        sigma_hist->GetXaxis()->SetRange(i, i);
        auto sigma_hist_proj = sigma_hist->Project3D("yz");
        sigma_hist_proj->SetTitle(Form("sigma_hist_proj_%d", i));
        sigma_hist_proj->Draw("colz");
        gPad->SetLogz();
        // canvas->Print(Form("fit_parameters_run%d.pdf", run_number));
        canvas->cd(5);
        N_hist->GetXaxis()->SetRange(i, i);
        auto N_hist_proj = N_hist->Project3D("yz");
        N_hist_proj->SetTitle(Form("N_hist_proj_%d", i));
        N_hist_proj->Draw("colz");
        gPad->SetLogz();
        // canvas->Print(Form("fit_parameters_run%d.pdf", run_number));
        canvas->cd(6);
        offset_hist->GetXaxis()->SetRange(i, i);
        auto offset_hist_proj = offset_hist->Project3D("yz");
        offset_hist_proj->SetTitle(Form("offset_hist_proj_%d", i));
        offset_hist_proj->Draw("colz");
        gPad->SetLogz();
        canvas->Print(Form("fit_parameters_run%d.pdf", run_number));

    }
    canvas->Print(Form("fit_parameters_run%d.pdf]", run_number));

    // Save a root file with the pedestal for each channel
    TFile *output_file = TFile::Open(Form("pedestals_run%d.root.new", run_number), "RECREATE");
    pedestals = new TH1F("pedestals", "Pedestals", 576, 0, 576);
    canvas->Clear();
    canvas->Print(Form("pedestals_run%d.pdf[", run_number));
    for (int i = 0; i < 576; i++) {
        auto profile = per_channel_waveform[i]->ProjectionY();
        int max_bin = profile->GetMaximumBin();
        double pedestal = profile->GetBinCenter(max_bin);

        profile->SetTitle(Form("Pedestal Channel %d", i));
        profile->Draw();
        TLatex latex;
        latex.SetNDC();
        latex.SetTextSize(0.03);
        latex.DrawLatex(0.55, 0.85, Form("Pedestal = %.2f", pedestal));
        canvas->Update();
        canvas->Print(Form("pedestals_run%d.pdf", run_number));
        // find the maximum bin
        pedestals->SetBinContent(i, pedestal);
    }
    canvas->Print(Form("pedestals_run%d.pdf]", run_number));
    pedestals->Write();
    output_file->Close();

    // Save all histograms to a root file
    output_file = TFile::Open(Form("waveforms_run%d.root", run_number), "RECREATE");
    for (int i = 0; i < 576; i++) {
        per_channel_waveform[i]->Write();
        fit_magnitude[i]->Write();
        muon_track_fit_magnitude[i]->Write();
        fit_integral[i]->Write();
        longitudinal_profile->Write();
        // alpha_hist->Write();
        // n_hist->Write();
        // x_bar_hist->Write();
        // sigma_hist->Write();
        // N_hist->Write();
        // offset_hist->Write();
    }
    output_file->Close();

    // Clean up
    delete canvas;
    file->Close();
    delete file;
}
