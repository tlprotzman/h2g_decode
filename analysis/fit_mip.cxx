#include <TROOT.h>
#include <TFile.h>
#include <TH1.h>
#include <TF1.h>
#include <TCanvas.h>
#include <TGraph.h>
#include <TGraphErrors.h>
#include <TLatex.h>
#include <TStyle.h>
#include <TError.h>

#include <vector>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>

void fit_mip() {
    gErrorIgnoreLevel = kWarning;
    gStyle->SetOptStat(0);
    // Open the file
    // TFile *file = TFile::Open("waveforms_run282.root");
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
    
    auto canvas = new TCanvas("canvas", "canvas", 1200, 600);
    // auto canvas = new TCanvas("canvas", "canvas", 800, 800);
    double x[576];
    double x_err[576];
    double mpv[576];
    double mpv_err[576];
    double width[576];
    double width_err[576];
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



    auto *dummy = new TH1F("dummy", "", 1, 0, 200);
    dummy->SetMinimum(0.001);
    dummy->SetMaximum(0.125);
    dummy->GetXaxis()->SetTitle("Crystal Ball Amplitude");
    dummy->GetYaxis()->SetTitle("Normalized Frequency");

    // We want to arrange things by the tiles actual coordinate.  Why not brute force it?
    int coordinates[64][4][2];  //(z, x, y)
    for (int i = 0; i < 576; i++) {
        if (channel_x[i] == -1) {
            continue;
        }
        int x, y, z;
        x = channel_x[i];
        y = channel_y[i];
        z = channel_z[i];
        coordinates[z][x][y] = i;
    }
    
    canvas->Clear();
    canvas->Divide(4, 2, 0, 0);
    // canvas->SetLeftMargin(0.25);
    // canvas->SetRightMargin(0.05);
    canvas->Print("mip_fit.pdf[");
    // for (int i = 0; i < 576; i++) {
    for (int tile_z = 0; tile_z < 64; tile_z++) {
        for (int tile_y = 0; tile_y < 2; tile_y++) {
            for (int tile_x = 0; tile_x < 4; tile_x++) {
                canvas->cd(1 + tile_x + 4 * tile_y);
                int i = coordinates[tile_z][tile_x][tile_y];
                if (channel_x[i] == -1) {   // Skip channels not connected to the detector
                    continue;
                }
                // if (histograms[i]->GetEntries() < 100) {    // Don't fit anything with fewer than 100 entries
                //     continue;
                // }
                // TF1 *fit = new TF1("fit", "landau + [3] * exp(-[4] * x)", 50, 600);
                histograms[i]->Scale(1.0 / histograms[i]->Integral());
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
                histograms[i]->Fit(fit, "QR0");
                histograms[i]->SetTitle(Form("Channel %d Spectra", i));
                // canvas->SetTopMargin(0.05);
                // canvas->SetRightMargin(0.05);
                dummy->Draw();
                dummy->GetXaxis()->SetTitleSize(0.05);
                dummy->GetXaxis()->SetTitleOffset(0.795);
                dummy->GetYaxis()->SetTitleSize(0.05);
                dummy->GetYaxis()->SetTitleOffset(0.99);
                histograms[i]->Draw("e same");
                fit->SetFillColorAlpha(kAzure+6, 0.15);
                fit->SetFillStyle(1001);
                // fit->Draw("same E3");
                auto fit_clone = (TF1*)fit->Clone();
                fit_clone->SetLineColor(kBlue);
                fit_clone->SetFillStyle(0);
                fit_clone->Draw("same");
                TLatex latex;
                latex.SetNDC();

                if (tile_x == 0 && tile_y == 0) {
                    latex.SetTextSize(0.06);
                    latex.SetTextFont();
                    latex.SetTextAlign(11);
                    latex.DrawLatex(0.14, 0.93, "LFHCal Test Beam");
                    latex.SetTextFont(42);
                    latex.SetTextSize(0.055);
                    latex.DrawLatex(0.14, 0.88, "CERN PS, 5 GeV#mu^{+}");
                    latex.DrawLatex(0.14, 0.83, Form("Layer %d", tile_z));
                    latex.DrawLatex(0.14, 0.78, "V_{op} = 43 V");
                }

                
                latex.SetTextSize(0.055);
                latex.SetTextAlign(31);
                latex.SetTextFont();
                latex.DrawLatex(0.97, 0.93, Form("Channel %d", i));
                latex.SetTextFont(42);
                // latex.DrawLatex(0.97, 0.85, Form("Amplitude = %.2f", fit->GetParameter(0)));
                latex.DrawLatex(0.97, 0.88, Form("MPV = %.2f", fit->GetParameter(1)));
                latex.DrawLatex(0.97, 0.83, Form("#sigma = %.2f", fit->GetParameter(2)));
                // latex.DrawLatex(0.97, 0.78, Form("#chi^{2}/NDF = %.2f", fit->GetChisquare() / fit->GetNDF()));
                latex.DrawLatex(0.97, 0.78, Form("Coordinate: (%d, %d, %d)", channel_x[i], channel_y[i], channel_z[i]));
                
                // latex.DrawLatex(0.875, 0.70, Form("Exponential Amplitude = %.2f", fit->GetParameter(3)));
                // latex.DrawLatex(0.875, 0.65, Form("Exponential Decay = %.2f", fit->GetParameter(4)));

                // latex.SetTextAlign(11);

                double chi2_per_ndf = fit->GetChisquare() / fit->GetNDF();
                chi2_ndf->Fill(chi2_per_ndf);
                if ((!std::isnan(chi2_per_ndf) && chi2_per_ndf < 2.5 && chi2_per_ndf > 0.5 && fit->GetParameter(1) > 11)) {
                    if (fit->GetParError(1) > 5) {
                        continue;
                    }
                    x[graph_lenght] = i;
                    x_err[graph_lenght] = 0;
                    mpv[graph_lenght] = fit->GetParameter(1);
                    mpv_err[graph_lenght] = fit->GetParError(1);
                    width[graph_lenght] = fit->GetParameter(2);
                    width_err[graph_lenght] = fit->GetParError(2);
                    graph_lenght++;
                }
                // gPad->SetLogy();
            }
        }
        canvas->Update();
        canvas->Print(Form("mip_fit_layer%d.png", tile_z));
        canvas->Print("mip_fit.pdf");
    }
    canvas->Print("mip_fit.pdf]");
    auto *dummy2 = new TH1F("dummy2", "", 1, -10, 586);
    dummy2->GetXaxis()->SetTitle("Channel");
    dummy2->GetYaxis()->SetTitle("MPV");
    dummy2->SetMinimum(0);
    dummy2->SetMaximum(80);


    delete canvas;
    canvas = new TCanvas("canvas", "canvas2", 800, 600);
    canvas->SetTopMargin(0.05);
    canvas->SetRightMargin(0.05);
    auto graph = new TGraphErrors(graph_lenght, x, mpv, x_err, mpv_err);
    gPad->SetLogy(0);
    graph->SetMarkerStyle(20);
    dummy2->Draw();
    graph->Draw("p same");
    graph->SetTitle("MPV vs Channel");
    graph->GetXaxis()->SetTitle("Channel");
    graph->GetYaxis()->SetTitle("MPV");
    graph->SetMinimum(0);
    graph->SetMaximum(80);

    TLatex latex;
    latex.SetNDC();
    latex.SetTextSize(0.04);
    latex.SetTextFont();
    latex.SetTextAlign(11);
    latex.DrawLatex(0.15, 0.9, "LFHCal Test Beam");
    latex.SetTextFont(42);
    latex.SetTextSize(0.035);
    latex.DrawLatex(0.15, 0.86, "CERN PS, 5 GeV#mu^{+}");
    latex.DrawLatex(0.15, 0.82, "V_{op} = 43 V");
    // latex.DrawLatex(0.15, 0.82, Form("Run %d Event %d Channel %d", run_number, i, channel));

    canvas->Print("mpv_per_channel.pdf");

    graph = new TGraphErrors(graph_lenght, x, width, x_err, width_err);
    gPad->SetLogy(0);
    graph->SetMarkerStyle(20);
    graph->Draw("ap");
    graph->SetTitle("Width vs Channel");
    graph->GetXaxis()->SetTitle("Channel");
    graph->GetYaxis()->SetTitle("Width");
    graph->SetMinimum(0);
    graph->SetMaximum(50);
    canvas->Print("width_per_channel.pdf");

    chi2_ndf->Draw("e");
    canvas->Print("chi2_ndf.pdf");
}