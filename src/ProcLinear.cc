#include <iostream>
#include <vector>
#include <memory>

#include <xercesc/dom/DOM.hpp>

#include "FWCore/Utilities/interface/Exception.h"

#include "PhysicsTools/MVAComputer/interface/AtomicId.h"

#include "PhysicsTools/MVATrainer/interface/XMLDocument.h"
#include "PhysicsTools/MVATrainer/interface/MVATrainer.h"
#include "PhysicsTools/MVATrainer/interface/TrainProcessor.h"
#include "PhysicsTools/MVATrainer/interface/LeastSquares.h"

XERCES_CPP_NAMESPACE_USE

using namespace PhysicsTools;

namespace { // anonymous

class ProcLinear : public TrainProcessor {
    public:
	typedef TrainProcessor::Registry<ProcLinear>::Type Registry;

	ProcLinear(const char *name, const AtomicId *id,
	           MVATrainer *trainer);
	virtual ~ProcLinear();

	virtual void configure(DOMElement *elem);
	virtual Calibration::VarProcessor *getCalibration() const;

	virtual void trainBegin();
	virtual void trainData(const std::vector<double> *values,
	                       bool target, double weight);
	virtual void trainEnd();

	virtual bool load();
	virtual void save();

    protected:
	virtual void *requestObject(const std::string &name) const;

    private:
	enum Iteration {
		ITER_FILL,
		ITER_DONE
	} iteration;

	std::auto_ptr<LeastSquares>	ls;
	std::vector<double>		vars;
};

static ProcLinear::Registry registry("ProcLinear");

ProcLinear::ProcLinear(const char *name, const AtomicId *id,
                             MVATrainer *trainer) :
	TrainProcessor(name, id, trainer),
	iteration(ITER_FILL)
{
}

ProcLinear::~ProcLinear()
{
}

void ProcLinear::configure(DOMElement *elem)
{
	ls = std::auto_ptr<LeastSquares>(new LeastSquares(getInputs().size()));
}

Calibration::VarProcessor *ProcLinear::getCalibration() const
{
	Calibration::ProcLinear *calib = new Calibration::ProcLinear;

	calib->coeffs = ls->getWeights();
	calib->offset = ls->getConstant();

	return calib;
}

void ProcLinear::trainBegin()
{
	if (iteration == ITER_FILL)
		vars.resize(ls->getSize());
}

void ProcLinear::trainData(const std::vector<double> *values,
                           bool target, double weight)
{
	if (iteration != ITER_FILL)
		return;

	for(unsigned int i = 0; i < ls->getSize(); i++, values++)
		vars[i] = values->front();

	ls->add(vars, target, weight);
}

void ProcLinear::trainEnd()
{
	switch(iteration) {
	    case ITER_FILL:
		vars.clear();
		ls->calculate();

		iteration = ITER_DONE;
		trained = true;
		break;
	    default:
		/* shut up */;
	}
}

void *ProcLinear::requestObject(const std::string &name) const
{
	if (name == "linearAnalyzer")
		return static_cast<void*>(ls.get());

	return 0;
}

bool ProcLinear::load()
{
	std::auto_ptr<XMLDocument> xml;

	try {
		xml = std::auto_ptr<XMLDocument>(new XMLDocument(
				trainer->trainFileName(this, "xml")));
	} catch(const XMLException &e) {
		return false;
	}

	DOMElement *elem = xml->getRootNode();
	if (std::strcmp(XMLSimpleStr(elem->getNodeName()), "ProcLinear") != 0)
		throw cms::Exception("ProcLinear")
			<< "XML training data file has bad root node."
			<< std::endl;

	DOMNode *node = elem->getFirstChild();
	while(node && node->getNodeType() != DOMNode::ELEMENT_NODE)
		node = node->getNextSibling();

	if (!node)
		throw cms::Exception("ProcLinear")
			<< "Train data file empty." << std::endl;

	ls->load(static_cast<DOMElement*>(node));

	node = elem->getNextSibling();
	while(node && node->getNodeType() != DOMNode::ELEMENT_NODE)
		node = node->getNextSibling();

	if (node)
		throw cms::Exception("ProcLinear")
			<< "Train data file contains superfluous tags."
			<< std::endl;

	iteration = ITER_DONE;
	trained = true;
	return true;
}

void ProcLinear::save()
{
	XMLDocument xml(trainer->trainFileName(this, "xml"), true);
	DOMDocument *doc = xml.createDocument("ProcLinear");

	xml.getRootNode()->appendChild(ls->save(doc));
}

} // anonymous namespace
