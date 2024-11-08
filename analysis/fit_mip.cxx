#include <TROOT.h>
#include <TFile.h>
#include <TH1.h>
#include <TF1.h>
#include <TCanvas.h>
#include <TGraph.h>
#include <TGraphErrors.h>
#include <TLatex.h>
#include <TError.h>

#include <vector>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>

void fit_mip() {
    gErrorIgnoreLevel = kWarning;
    // Open the file
    // TFile *file = TFile::Open("waveforms_run276.root");
    TFile *file = TFile::Open("muon_runs/all_muons.root");
    std::vector<TH1F*> histograms;
    for (int i = 0; i < 576; i++) {
        histograms.push_back(nullptr);
        file->GetObject(Form("muon_cb_norm_%d", i), histograms.back());
    }

    TFile *output_file = TFile::Open("mip_fit.root", "RECREATE");
    TH1F *mpv_hist = new TH1F("mpv_hist", "MPV;Channel;Value", 576, 0, 576);
    TH1F *sigma_hist = new TH1F("sigma_hist", "Sigma;Channel;Value", 576, 0, 576);
    TH1F *integral_hist = new TH1F("integral_hist", "Integral;Channel;Value", 576, 0, 576);
    // Fit the histogram

    auto chi2_ndf = new TH1F("chi2_ndf", "Chi^2/NDF;Channel;Value", 50, 0.2, 1.8);
    
    auto canvas = new TCanvas("canvas", "canvas", 800, 600);
    int x[576];
    int x_err[576];
    int y[576];
    int y_err[576];
    int graph_lenght = 0;

    // Read in the channel mapping csv file
    int channel_x[576];
    int channel_y[576];
    int channel_z[576];
    std::ifstream channel_map_file("lfhcal_hgcroc.mapping");
    std::string line;
    std::getline(channel_map_file, line); // skip header
    while (std::getline(channel_map_file, line)) {
        std::stringstream ss(line);
        std::string token;
        std::vector<std::string> tokens;
        while (std::getline(ss, token, ' ')) {
            tokens.push_back(token);
        }
        int channel = std::stoi(tokens[0]);
        channel_x[channel] = std::stoi(tokens[1]);
        channel_y[channel] = std::stoi(tokens[2]);
        channel_z[channel] = std::stoi(tokens[3]);
    }
    channel_x[0] = 3;
    channel_y[0] = 0;
    channel_z[0] = 23;

    // std::cout << "channel 0: " << channel_x[0] << " " << channel_y[0] << " " << channel_z[0] << std::endl;
    // std::cout << "channel 1: " << channel_x[1] << " " << channel_y[1] << " " << channel_z[1] << std::endl;



    
    
    canvas->Print("mip_fit.pdf[");
    for (int i = 0; i < 576; i++) {
        if (channel_x[i] == -1) {   // Skip channels not connected to the detector
            continue;
        }
        if (histograms[i]->GetEntries() < 100) {    // Don't fit anything with fewer than 100 entries
            continue;
        }
        // TF1 *fit = new TF1("fit", "landau + [3] * exp(-[4] * x)", 50, 600);
        TF1 *fit = new TF1("fit", "landau", 5, 200);
        fit->SetParameter(0, 400);
        fit->SetParLimits(0, 0, 1000);    // landau amplitude
        fit->SetParameter(1, 10);
        fit->SetParLimits(1, 5, 250);   // landau mpv
        fit->SetParameter(2, 35);
        fit->SetParLimits(2, 0.4, 10000);  // landau width
        // fit->SetParameter(3, 100);
        // fit->SetParLimits(3, 0, 10000);   // exponential amplitude
        // fit->SetParameter(4, 0.05);
        // fit->SetParLimits(4, 0.03, 0.07);   // exponential decay constant
        histograms[i]->Fit(fit, "QR");
        histograms[i]->SetTitle(Form("Channel %d Spectra", i));
        histograms[i]->Draw("e");
        TLatex latex;
        latex.SetNDC();
        latex.SetTextSize(0.03);
        latex.SetTextAlign(31);
        latex.DrawLatex(0.875, 0.85, Form("Amplitude = %.2f", fit->GetParameter(0)));
        latex.DrawLatex(0.875, 0.80, Form("MPV = %.2f", fit->GetParameter(1)));
        latex.DrawLatex(0.875, 0.75, Form("Sigma = %.2f", fit->GetParameter(2)));
        // latex.DrawLatex(0.875, 0.70, Form("Exponential Amplitude = %.2f", fit->GetParameter(3)));
        // latex.DrawLatex(0.875, 0.65, Form("Exponential Decay = %.2f", fit->GetParameter(4)));

        latex.DrawLatex(0.875, 0.70, Form("Chi^2/NDF = %.2f", fit->GetChisquare() / fit->GetNDF()));
        latex.DrawLatex(0.875, 0.65, Form("Coordinate: (%d, %d, %d)", channel_x[i], channel_y[i], channel_z[i]));
        // latex.SetTextAlign(11);

        chi2_ndf->Fill(fit->GetChisquare() / fit->GetNDF());
        x[graph_lenght] = i;
        x_err[graph_lenght] = 0;
        y[graph_lenght] = fit->GetParameter(1);
        y_err[graph_lenght] = fit->GetParError(1);
        graph_lenght++;
        gPad->SetLogy();
        canvas->Print("mip_fit.pdf");
    }
    canvas->Print("mip_fit.pdf]");
    auto graph = new TGraph(graph_lenght, x, y);
    gPad->SetLogy(0);
    graph->SetMarkerStyle(20);
    graph->Draw("ap");
    graph->SetTitle("MPV vs Channel");
    graph->GetXaxis()->SetTitle("Channel");
    graph->GetYaxis()->SetTitle("MPV");
    canvas->Print("mpv_per_channel.pdf");

    chi2_ndf->Draw("e");
    canvas->Print("chi2_ndf.pdf");
}