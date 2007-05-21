#include <unistd.h>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <fstream>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <vector>
#include <memory>

#include <xercesc/dom/DOM.hpp>

#include <TDirectory.h>
#include <TTree.h>
#include <TFile.h>
#include <TCut.h>

#include <TMVA/Types.h>
#include <TMVA/Factory.h>

#include "FWCore/Utilities/interface/Exception.h"

#include "PhysicsTools/MVAComputer/interface/AtomicId.h"
#include "PhysicsTools/MVAComputer/interface/memstream.h"
#include "PhysicsTools/MVAComputer/interface/zstream.h"

#include "PhysicsTools/MVATrainer/interface/XMLDocument.h"
#include "PhysicsTools/MVATrainer/interface/XMLSimpleStr.h"
#include "PhysicsTools/MVATrainer/interface/MVATrainer.h"
#include "PhysicsTools/MVATrainer/interface/SourceVariable.h"
#include "PhysicsTools/MVATrainer/interface/TrainProcessor.h"

XERCES_CPP_NAMESPACE_USE

using namespace PhysicsTools;

namespace { // anonymous

class ROOTContextSentinel {
    public:
	ROOTContextSentinel() : dir(gDirectory), file(gFile) {}
	~ROOTContextSentinel() { gDirectory = dir; gFile = file; }

    private:
	TDirectory	*dir;
	TFile		*file;
};

class ProcTMVA : public TrainProcessor {
    public:
	typedef TrainProcessor::Registry<ProcTMVA>::Type Registry;

	ProcTMVA(const char *name, const AtomicId *id,
	         MVATrainer *trainer);
	virtual ~ProcTMVA();

	virtual void configure(DOMElement *elem);
	virtual Calibration::VarProcessor *getCalibration() const;

	virtual void trainBegin();
	virtual void trainData(const std::vector<double> *values,
	                       bool target, double weight);
	virtual void trainEnd();

	virtual bool load();
	virtual void cleanup();

    private:
	void runTMVATrainer();

	std::string getTreeName() const
	{ return trainer->getName() + '_' + (const char*)getName(); }
	std::string getWeightsFile(const char *ext) const
	{
		return "weights/" + getTreeName() + '_' +
		       methodName + ".weights." + ext;
	}

	enum Iteration {
		ITER_EXPORT,
		ITER_DONE
	} iteration;

	TMVA::Types::EMVA		methodType;
	std::string			methodName;
	std::string			methodDescription;
	std::vector<std::string>	names;
	std::auto_ptr<TFile>		file;
	TTree				*tree;
	Bool_t				target;
	Double_t			weight;
	std::vector<Double_t>		vars;
	bool				needCleanup;
	unsigned int			nSignal;
	unsigned int			nBackground;
};

static ProcTMVA::Registry registry("ProcTMVA");

ProcTMVA::ProcTMVA(const char *name, const AtomicId *id,
                   MVATrainer *trainer) :
	TrainProcessor(name, id, trainer),
	iteration(ITER_EXPORT), tree(0), needCleanup(false)
{
}

ProcTMVA::~ProcTMVA()
{
}

void ProcTMVA::configure(DOMElement *elem)
{
	std::vector<SourceVariable*> inputs = getInputs().get();

	for(std::vector<SourceVariable*>::const_iterator iter = inputs.begin();
	    iter != inputs.end(); iter++) {
		std::string name = (const char*)(*iter)->getName();

		if (std::find(names.begin(), names.end(), name)
		    != names.end()) {
			for(unsigned i = 1;; i++) {
				std::ostringstream ss;
				ss << name << "_" << i;
				if (std::find(names.begin(), names.end(),
				              ss.str()) == names.end()) {
					name == ss.str();
					break;
				}
			}
		}

		names.push_back(name);
	}

	DOMNode *node = elem->getFirstChild();
	while(node && node->getNodeType() != DOMNode::ELEMENT_NODE)
		node = node->getNextSibling();

	if (!node)
		throw cms::Exception("ProcTMVA")
			<< "Expected TMVA method in config section."
			<< std::endl;

	if (std::strcmp(XMLSimpleStr(node->getNodeName()), "method") != 0)
		throw cms::Exception("ProcTMVA")
				<< "Expected method tag in config section."
				<< std::endl;

	elem = static_cast<DOMElement*>(node);

	methodType = TMVA::Types::Instance().GetMethodType(
		XMLDocument::readAttribute<std::string>(elem,
		                                        "type").c_str());

	methodName = XMLDocument::readAttribute<std::string>(elem, "name");

	methodDescription = (const char*)XMLSimpleStr(node->getTextContent());

	node = node->getNextSibling();
	while(node && node->getNodeType() != DOMNode::ELEMENT_NODE)
		node = node->getNextSibling();

	if (node)
		throw cms::Exception("ProcTMVA")
			<< "Superfluous tags in config section."
			<< std::endl;
}

bool ProcTMVA::load()
{
	bool ok = false;
	/* test for weights file */ {
		std::ifstream in(getWeightsFile("txt").c_str());
		ok = in.good();
	}

	if (!ok)
		return false;

	iteration = ITER_DONE;
	trained = true;
	return true;
}

static std::size_t getStreamSize(std::ifstream &in)
{
	std::ifstream::pos_type begin = in.tellg();
	in.seekg(0, std::ios::end);
	std::ifstream::pos_type end = in.tellg();
	in.seekg(begin, std::ios::beg);

	return (std::size_t)(end - begin);
}

Calibration::VarProcessor *ProcTMVA::getCalibration() const
{
	Calibration::ProcTMVA *calib = new Calibration::ProcTMVA;

	std::ifstream in(getWeightsFile("txt").c_str(),
	                 std::ios::binary | std::ios::in);
	if (!in.good())
		throw cms::Exception("ProcTMVA")
			<< "Weights file " << getWeightsFile("txt")
			<< " cannot be opened for reading." << std::endl;

	std::size_t size = getStreamSize(in);
	size = size + (size / 32) + 128;

	char *buffer = 0;
	try {
		buffer = new char[size];
		ext::omemstream os(buffer, size);
		/* call dtor of ozs at end */ {
			ext::ozstream ozs(&os);
			ozs << in.rdbuf();
			ozs.flush();
		}
		size = os.end() - os.begin();
		calib->store.resize(size);
		std::memcpy(&calib->store.front(), os.begin(), size);
	} catch(...) {
		delete[] buffer;
		throw;
	}
	delete[] buffer;
	in.close();

	calib->method = methodName;
	calib->variables = names;

	return calib;
}

void ProcTMVA::trainBegin()
{
	if (iteration == ITER_EXPORT) {
		ROOTContextSentinel ctx;

		file = std::auto_ptr<TFile>(TFile::Open(
			trainer->trainFileName(this, "root",
			                       "input").c_str(),
			"RECREATE"));
		if (!file.get())
			throw cms::Exception("ProcTMVA")
				<< "Could not open ROOT file for writing."
				<< std::endl;

		file->cd();
		tree = new TTree(getTreeName().c_str(), "MVATrainer");

		tree->Branch("__TARGET__", &target, "__TARGET__/B");
		tree->Branch("__WEIGHT__", &weight, "__WEIGHT__/D");

		vars.resize(names.size());

		std::vector<Double_t>::iterator pos = vars.begin();
		for(std::vector<std::string>::const_iterator iter =
			names.begin(); iter != names.end(); iter++, pos++)
			tree->Branch(iter->c_str(), &*pos,
			             (*iter + "/D").c_str());

		nSignal = nBackground = 0;
	}
}

void ProcTMVA::trainData(const std::vector<double> *values,
                         bool target, double weight)
{
	if (iteration != ITER_EXPORT)
		return;

	this->target = target;
	this->weight = weight;
	for(unsigned int i = 0; i < vars.size(); i++, values++)
		vars[i] = values->front();

	tree->Fill();

	if (target)
		nSignal++;
	else
		nBackground++;
}

void ProcTMVA::runTMVATrainer()
{
	needCleanup = true;

	if (nSignal < 1 || nBackground < 1)
		throw cms::Exception("ProcTMVA")
			<< "Not going to run TMVA: "
			   "No signal or background events!" << std::endl;

	std::auto_ptr<TFile> file(std::auto_ptr<TFile>(TFile::Open(
		trainer->trainFileName(this, "root", "output").c_str(),
		"RECREATE")));
	if (!file.get())
		throw cms::Exception("ProcTMVA")
			<< "Could not open TMVA ROOT file for writing."
			<< std::endl;

	std::auto_ptr<TMVA::Factory> factory(
		new TMVA::Factory(getTreeName().c_str(), file.get(), ""));

	if (!factory->SetInputTrees(tree, TCut("__TARGET__"),
	                                  TCut("!__TARGET__")))
		throw cms::Exception("ProcTMVA")
			<< "TMVA rejecte input trees." << std::endl;

	for(std::vector<std::string>::const_iterator iter = names.begin();
	    iter != names.end(); iter++)
		factory->AddVariable(iter->c_str(), 'D');

	factory->SetWeightExpression("__WEIGHT__");

	factory->PrepareTrainingAndTestTree("", -1);

	factory->BookMethod(methodType, methodName, methodDescription);

	factory->TrainAllMethods();

	file->Close();
}

void ProcTMVA::trainEnd()
{
	switch(iteration) {
	    case ITER_EXPORT:
		/* ROOT context-safe */ {
			ROOTContextSentinel ctx;
			file->cd();
			tree->Write();

			runTMVATrainer();

			file->Close();
			tree = 0;
			file.reset();
		}
		vars.clear();

		iteration = ITER_DONE;
		trained = true;
		break;
	    default:
		/* shut up */;
	}
}

void ProcTMVA::cleanup()
{
	if (!needCleanup)
		return;

	std::remove(trainer->trainFileName(this, "root", "input").c_str());
	std::remove(trainer->trainFileName(this, "root", "output").c_str());
	std::remove(getWeightsFile("txt").c_str());
	std::remove(getWeightsFile("root").c_str());
	rmdir("weights");
}

} // anonymous namespace
