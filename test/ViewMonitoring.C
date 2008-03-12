#include <iostream>
#include <stdlib.h>

#include <TFile.h>
#include <TList.h>
#include <TString.h>
#include <TSystem.h>
#include <TObject.h>
#include <TObjString.h>
#include <TIterator.h>
#include <TControlBar.h>
#include <TGWindow.h>
#include <TGMenu.h>
#include <TGClient.h>
#include <TGResourcePool.h>
#include <TVirtualX.h>

static TFile *file = 0;
static TStyle *style = 0;
static const TGWindow *root = 0;

static Double_t Min(Double_t a, Double_t b)
{ return a < b ? a : b; }

static Double_t Max(Double_t a, Double_t b)
{ return a > b ? a : b; }

class SelectMenu : public TGPopupMenu {
    public:
	SelectMenu(TString what);

	void HandleMenu(Int_t id);

    private:
	TString	mode;
	TList	entries;
};

static void ShowMenu(TString what);

void ViewMonitoring(TString fileName = "train_monitoring.root")
{
	file = TFile::Open(fileName);
	if (!file)
		abort();
	root = gClient->GetRoot();

	TControlBar *main =
		new TControlBar("vertical", "MVA Trainer Monitoring", 0, 0);

	style = gROOT->GetStyle("Plain");
	style->SetCanvasColor(0);
	style->SetLineStyleString(5, "[52 12]");
	style->SetLineStyleString(6, "[22 12]");
	style->SetLineStyleString(7, "[22 10 7 10]");
	style->cd();

	main->AddButton("input variables", "ShowMenu(\"input\")",
	                "plots input variables for variable processors",
	                "button");

	main->AddButton("ProcNormalize", "ShowMenu(\"ProcNormalize\")",
	                "show normalizer PDF distributions", "button");

	main->Show();
}

SelectMenu::SelectMenu(TString what) : mode(what)
{
	AddLabel(what + " plots");
	AddSeparator();

	unsigned int n = 0;
	TIter iter(file->GetListOfKeys());
	TObject *obj = 0;
	while((obj = iter.Next()) != 0) {
		TString name = obj->GetName();
		if (!name.BeginsWith(what + "_"))
			continue;

		entries.Add(new TObjString(name));

		Int_t len = what.Length();
		name = name(len + 1, name.Length() - (len + 1));

		AddEntry(name, n++);
	}

	if (!n)
		AddEntry("no entry", -1);

	Connect("Activated(Int_t)", "SelectMenu", this, "HandleMenu(Int_t)");
}

void ShowMenu(TString what)
{
	SelectMenu *menu = new SelectMenu(what);

	Window_t dum1, dum2;
	Int_t xroot, yroot, x, y;
	UInt_t state;
	gVirtualX->QueryPointer((Window_t)root->GetId(),
	                        dum1, dum2, xroot, yroot, x, y, state);

	menu->Move(x - 10, y - 10);
	menu->MapWindow();

	gVirtualX->GrabPointer((Window_t)menu->GetId(),
	                       TGWidget::kButtonPressMask |
	                       TGWidget::kButtonReleaseMask |
	                       TGWidget::kPointerMotionMask,
	                       TGWidget::kNone,
	                       gClient->GetResourcePool()->GetGrabCursor());
}

void SelectMenu::HandleMenu(Int_t id)
{
	if (id < 0)
		return;

	TString name = ((TObjString*)entries.At(id))->GetString();

	TDirectory *dir = dynamic_cast<TDirectory*>(file->Get(name));
	if (mode == "input")
		DrawInputs(dir);
	else if (mode == "ProcNormalize")
		DrawProcNormalize(dir);
}

class PadService {
    public:
	PadService(TString name, TString title, Int_t nPlots);
	~PadService();

	TVirtualPad *Next();

    private:
	TCanvas	*last;
	Int_t	index, count;

	TString	name, title;

	Int_t	width, height;
	Int_t	nPadsX, nPadsY;
};

PadService::PadService(TString name, TString title, Int_t nPlots) :
	last(0), index(0), count(0), name(name), title(title)
{
	switch (nPlots) {
	    case 1:
		nPadsX = 1; nPadsY = 1; width = 500; height = 0.55 * width;
		break;
	    case 2:
		nPadsX = 2; nPadsY = 1; width = 600; height = 0.55 * width;
		break;
	    case 3:
		nPadsX = 3; nPadsY = 1; width = 900; height = 0.40 * width;
		break;
	    case 4:
		nPadsX = 2; nPadsY = 2; width = 600; height = 1.00 * width;
		break;
	    default:
		nPadsX = 3; nPadsY = 2; width = 800; height = 0.55 * width;
		break;
	}
}

PadService::~PadService()
{
}

TVirtualPad *PadService::Next()
{
	if (!last) {
		last = new TCanvas(Form("%s_%d", (const char*)name,
		                        ++count),
		                   title, count * 50 + 200, count * 20,
		                   width, height);
		last->SetBorderSize(2);
		last->SetFrameFillColor(0);
		last->SetHighLightColor(0);
		last->Divide(nPadsX, nPadsY);
		index = 0;
		count++;
	}

	last->cd(++index);
	TVirtualPad *pad = last->GetPad(index);

	if (index == nPadsX * nPadsY)
		last = 0;

	return pad;
}

void Save(TVirtualPad *pad, TDirectory *dir, TString name)
{
	gSystem->mkdir("plots");
	TString baseName = TString("plots/") + dir->GetName() + "." + name;

	pad->Print(baseName + ".eps");
	pad->Print(baseName + ".png");
}

void DrawInputs(TDirectory *dir)
{
	TList *keys = dir->GetListOfKeys();
	TString name = dir->GetName();
	name = name(6, name.Length() - 6);

	PadService pads(dir->GetName(), "\"" + name + "\" input variables",
	                keys->GetSize() / 2);

	TIter iter(keys);
	TObject *obj = 0;
	while((obj = iter.Next()) != 0) {
		TString name = obj->GetName();
		if (!name.EndsWith("_sig"))
			continue;
		name = name(0, name.Length()-4);

		TH1 *bkg = dynamic_cast<TH1*>(dir->Get(name + "_bkg"));
		TH1 *sig = dynamic_cast<TH1*>(dir->Get(name + "_sig"));

		if (!bkg || !sig)
			continue;

		TVirtualPad *pad = pads.Next();

		bkg = (TH1*)bkg->Clone(name + "_tmp1");
		bkg->Scale(1.0 / bkg->Integral("width"));

		sig = (TH1*)sig->Clone(name + "_tmp1");
		sig->Scale(1.0 / sig->Integral("width"));

		Double_t x1 = Min(bkg->GetXaxis()->GetXmin(),
		                  sig->GetXaxis()->GetXmin());
		Double_t x2 = Max(bkg->GetXaxis()->GetXmax(),
		                  sig->GetXaxis()->GetXmax());
		Double_t y = Max(bkg->GetMaximum(),
		                 sig->GetMaximum());
		TH1F *tmp = new TH1F(name + "_tmp3", name, 1, x1, x2);
		tmp->SetBit(kCanDelete);
		tmp->SetStats(0);
		tmp->GetYaxis()->SetRangeUser(0.0, y * 1.05);
		tmp->SetXTitle(name);
		tmp->Draw();
		bkg->SetFillColor(2);
		bkg->SetLineColor(2);
		bkg->SetLineWidth(2);
		bkg->SetFillStyle(3554);
		bkg->Draw("same");
		sig->SetFillColor(0);
		sig->SetLineColor(4);
		sig->SetLineWidth(2);
		sig->SetFillStyle(1);
		sig->Draw("same");

		Save(pad, dir, name);
	}
}

void DrawProcNormalize(TDirectory *dir)
{
	TList *keys = dir->GetListOfKeys();
	TString name = dir->GetName();
	name = name(14, name.Length() - 14);

	PadService pads(dir->GetName(),
	                "\"" + name + "\" normalization PDFs",
	                keys->GetSize());

	TIter iter(keys);
	TObject *obj = 0;
	while((obj = iter.Next()) != 0) {
		TString name = obj->GetName();

		TH1 *pdf = dynamic_cast<TH1*>(dir->Get(name));
		if (!pdf)
			continue;

		TVirtualPad *pad = pads.Next();

		pdf = (TH1*)pdf->Clone(name + "_tmpPN1");
		pdf->Scale(1.0 / pdf->Integral("width"));
		pdf->SetStats(0);
		pdf->SetFillColor(4);
		pdf->SetLineColor(4);
		pdf->SetLineWidth(0);
		pdf->SetFillStyle(3554);
		pdf->Draw("C");

		TH1 *pdf2 = (TH1*)pdf->Clone(name + "_tmpPN2");
		pdf2->SetFillStyle(0);
		pdf2->SetLineWidth(2);
		pdf2->Draw("C same");

		Save(pad, dir, name);
	}
}
