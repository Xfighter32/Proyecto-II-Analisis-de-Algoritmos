#include "genetic-algorithm.h"
#include "random.cpp"
#include <typeinfo>

// PARA IMPRIMIR

// rapidjson::StringBuffer sb;
// rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(sb);
// stretch->Accept(writer);
// auto str = sb.GetString();
// printf("%s\n", str);

// while (!this->population.empty())
// {
// 	Wheel* wheel = this->population.front();
// 	this->rankedPopulation.push(wheel);
// 	this->population.pop();
// }

// while (!rankedPopulation.empty())
// {
// 	std::cout << "[Torque: " << rankedPopulation.top()->getTorqueId() <<
// 		", Tread: " << rankedPopulation.top()->getTreadId() <<
// 		", Fitness Score: " << rankedPopulation.top()->getFitnessScore()
// 		<< "]" << std::endl;
// 	rankedPopulation.pop();
// }

GeneticAlgorithm::GeneticAlgorithm(Vehicle* pVehicle, const rapidjson::Document& pConfig, SyncQueue* pSharedQueue)
{
	this->vehicle = pVehicle;
	this->fittestExtractionPercentage = (float) pConfig["geneticAlgoConfig"].GetObject()["fittestExtractionPercentage"].GetDouble();
	this->populationAmount = pConfig["geneticAlgoConfig"].GetObject()["populationAmount"].GetInt();
	this->numberOfExtractions = populationAmount * fittestExtractionPercentage;
	this->sensorWaitTime = pConfig["geneticAlgoConfig"].GetObject()["sensorWaitTime"].GetInt();
	this->convergencePercentage = (float) pConfig["geneticAlgoConfig"].GetObject()["convergencePercentage"].GetDouble();
	this->mutationPercentage = pConfig["geneticAlgoConfig"].GetObject()["mutationPercentage"].GetInt();
	this->totalDistance = pConfig["simulationConfig"].GetObject()["distanceGoal"].GetInt();
	this->specificationWiseFitness = pConfig["geneticAlgoConfig"].GetObject()["specificationWiseFitness"].GetBool();
	this->distanceProcessed = 0;
	this->queue = pSharedQueue;	
	this->generation = 0;
	this->setSpecifications(pConfig);
}

GeneticAlgorithm::~GeneticAlgorithm()
{
	while (!this->population.empty())
	{
		this->population.pop();
	}
}

void GeneticAlgorithm::loadSpecificationTable(const rapidjson::Document &pConfig, 
											  std::unordered_map<int,Specification*> &pHashTable, 
											  const char* pTableName)
{
	for (auto& atribute : pConfig[pTableName].GetArray())
	{
		int id = atribute.GetObject()["id"].GetInt();
		int energy = atribute.GetObject()["energy"].GetInt();
		std::vector<int> ranges;
		for (auto& magnitude : atribute.GetObject()["ranges"].GetObject())
		{
			ranges.push_back(magnitude.value.GetArray()[0].GetInt());
			ranges.push_back(magnitude.value.GetArray()[1].GetInt());
		}
		pHashTable[id] = new Specification(id,ranges,energy);
	}
}

void GeneticAlgorithm::setSpecifications(const rapidjson::Document& pConfig)
{
	loadSpecificationTable(pConfig, this->torqueTable, "torqueTable");
	loadSpecificationTable(pConfig, this->treadTable, "treadTable");
}

void GeneticAlgorithm::getStretch()
{

	if (this->distanceProcessed < this->totalDistance)
	{
		rapidjson::Document* stretch = this->queue->pop();
		int distance = 0;
		
		for (const auto& terrainJSONObject : stretch->GetArray())
		{
			Terrain* terrain = new Terrain(terrainJSONObject);
			this->currentStretch.push_back(terrain);
			distance += (terrain->getEndKm() - terrain->getStartKm());
		}
		this->distanceProcessed += distance;
		std::cout << "A new stretch was popped from the queue!" << std::endl;
	}
}

void GeneticAlgorithm::setCurrentTerrain()
{
	if (!this->currentStretch.empty())
	{
		this->currentTerrain = this->currentStretch[0];
		this->currentStretch.erase(this->currentStretch.begin());
		this->currentTerrain->print();
	}
}

void GeneticAlgorithm::startPopulation()
{
	this->generation = 0;
	for (int amount = 0; amount < this->populationAmount; amount++)
	{
		this->population.push(new Wheel(Random::RandomChromosome()));
	}
}

void GeneticAlgorithm::setIndividualFitness(Wheel* pWheel)
{
	// std::cout << "Torque Id for wheel : " << pWheel->getTorqueId() << std::endl;
	// std::cout << "Tread Id for wheel : " << pWheel->getTreadId() << std::endl;

	Specification* torque = this->torqueTable[pWheel->getTorqueId()];
	Specification* tread = this->treadTable[pWheel->getTreadId()];

	std::vector<float> terrainAttributes = this->currentTerrain->getAttributes();
	std::vector<int> torqueAttributes;
	torque->getClosestAttributesTo(terrainAttributes, torqueAttributes);
	std::vector<int> treadAttributes;
	tread->getClosestAttributesTo(terrainAttributes, treadAttributes);

	double terrainNorm = sqrt(
						 pow(terrainAttributes[0], 2) + 
						 pow(terrainAttributes[1], 2) + 
					 	 pow(terrainAttributes[2], 2)
						 );
	double torqueNorm = sqrt(
						pow(torqueAttributes[0], 2) + 
						pow(torqueAttributes[1], 2) + 
						pow(torqueAttributes[2], 2)
						);
	double treadNorm = sqrt(
					   pow(treadAttributes[0], 2) + 
					   pow(treadAttributes[1], 2) + 
					   pow(treadAttributes[2], 2)
					   );

	double fitnessScore = 0.0;
	double torqueSimilarity = 0.0;
	double treadSimilarity = 0.0;
	double overallSimilarity = 0.0;

	for (int fitnessIndex = 0; fitnessIndex < 3; fitnessIndex++)
	{
		torqueSimilarity += (torqueAttributes[fitnessIndex] * terrainAttributes[fitnessIndex]);
		treadSimilarity +=  (treadAttributes[fitnessIndex] * terrainAttributes[fitnessIndex]);
		overallSimilarity += (treadAttributes[fitnessIndex] * torqueAttributes[fitnessIndex] * terrainAttributes[fitnessIndex]);
	}

	torqueSimilarity = torqueSimilarity / (terrainNorm * torqueNorm);
	treadSimilarity = treadSimilarity / (terrainNorm * treadNorm);
	overallSimilarity = overallSimilarity / (terrainNorm * torqueNorm * treadNorm);

	pWheel->setTorqueEnergyPercentile(torque->getEnergy());
	pWheel->setTreadEnergyPercentile(tread->getEnergy());
	pWheel->setOverallSimilarity(overallSimilarity);
	pWheel->setTorqueSimilarity(torqueSimilarity);
	pWheel->setTreadSimilarity(treadSimilarity);

	if (this->specificationWiseFitness)
   		fitnessScore = (1/torqueSimilarity)*torque->getEnergy() + (1/treadSimilarity)*tread->getEnergy();
   	else
   		fitnessScore = (1/overallSimilarity) * (torque->getEnergy() + tread->getEnergy());

	pWheel->setFitnessScore(fitnessScore);
	pWheel->setEnergeticCost(torque->getEnergy() + tread->getEnergy());
}

void GeneticAlgorithm::setPopulationFitness()
{
	int wheelsInserted = 0;
	while (wheelsInserted < this->populationAmount)
	{
		Wheel * wheel = this->population.front();
		setIndividualFitness(wheel);
		this->rankedPopulation.push(wheel);
		this->population.pop();
		wheelsInserted++;
	}
}

bool GeneticAlgorithm::checkConvergence()
{
	int highest = 0;
	int convergencePoint = this->populationAmount * this->convergencePercentage;
	for (auto & frequency : this->frequencyTable)
	{
		if (frequency.second > highest)
			highest = frequency.second;
		if (frequency.second >= convergencePoint)
		{
			std::cout << "highest: " << highest << std::endl;
			addWheelToVehicle(frequency.first);
			resetContainers();
			return true;
		}
		
		frequency.second = 0;
	}
	std::cout << "highest: " << highest << std::endl;
	return false;
}

void GeneticAlgorithm::addWheelToVehicle(int pWheelConfiguration)
{
	int torqueId = pWheelConfiguration % 10;
	int treadId = pWheelConfiguration / 10;
	Wheel * wheel = new Wheel(torqueId, treadId);
	setIndividualFitness(wheel);
	this->vehicle->addWheelForTerrain(this->currentTerrain, wheel);
}

void GeneticAlgorithm::resetContainers()
{
	// frequency table and ranked population must be cleared for next genetic cycle

	for (auto& frequency : this->frequencyTable)
	{
		frequency.second = 0;
	}

	while (!this->rankedPopulation.empty())
	{
		rankedPopulation.pop();
	}
}

void GeneticAlgorithm::startEvolution()
{
	srand(time(0));
	do
	{
		getStretch();
		while (!currentStretch.empty() && vehicle->hasEnergy()) 
		{
			setCurrentTerrain();
			startPopulation();
			setPopulationFitness();

			do
			{				
				evolve();
			} 
			while (!checkConvergence());

			std::this_thread::sleep_for(std::chrono::seconds(sensorWaitTime));
		}
	} 
	while (!queue->empty() && vehicle->hasEnergy());

	if (vehicle->hasEnergy())
		std::cout << "You have arrived succesfully at destination!!" << std::endl;
	else
		std::cout << "Bummer ... The battery ran out before reaching your destination" << std::endl;

	vehicle->printRouteConfiguration();
}

std::queue<Wheel*> GeneticAlgorithm::selectFittestParents()
{
	std::queue<Wheel*> fittest;
	for (int i = 0; i < numberOfExtractions; i++)
	{
		fittest.push(rankedPopulation.top());
		rankedPopulation.pop();
	}
	return fittest;
}

void showbits(std::string text, unsigned int x )
{
 int i=0;
 std::cout<<text;
 for (i = (sizeof(int) * 8) - 1; i >= 0; i--)
 {
    putchar(x & (1u << i) ? '1' : '0');
 }
 printf("\n");
}

void printTest(std::string text, int value)
{
 std::cout << text << value <<std::endl;
}

Wheel* GeneticAlgorithm::crossover(Wheel* pParent1, Wheel* pParent2)	// crossover normal, pero con 1 hijo
{
	int position = Random::RandomRange(8,24);
	generateMasks(position);
	// Wheel* parents[2] = { pParent1,pParent2 };
	unsigned int childGenotype = 0;
	unsigned int temp = 0;

	temp = pParent1->getChromosome() & leftMask;	// get tread from parent
	childGenotype = childGenotype | temp;
	temp = 0;
	temp = pParent2->getChromosome() & rightMask; // get torque from parent
	childGenotype = childGenotype | temp;
	temp = 0;
	// showbits("childGenotype: ",childGenotype);
	return new Wheel(childGenotype);
	// childGenotype = 0;
}

void GeneticAlgorithm::generateMasks(int pBytePos)
{
	int rightMaskShift = 32 - pBytePos;
	unsigned int base = 0b11111111111111111111111111111111;
	leftMask = base << pBytePos;
	base = 0b11111111111111111111111111111111;
	rightMask = base >> rightMaskShift;
	// showbits("LM", leftMask);
	// showbits("RM", rightMask);
}

void GeneticAlgorithm::mutate(Wheel * pChild)
{
	unsigned int pos = Random::RandomRangeInt(0, 32);
 	unsigned int mask = 1 << pos; 
	unsigned int childChromosome = pChild->getChromosome();
 	bool has_bit = childChromosome & mask;
	unsigned int newChromosome = (childChromosome & ~mask) | ((!has_bit << pos) & mask);
	pChild->setChromosome(newChromosome);
}

void GeneticAlgorithm::tryMutation(Wheel * pChild)
{
	int mutationSucces = Random::RandomRange(0, 100);
	if (mutationSucces < mutationPercentage) 
		mutate(pChild);
}

void GeneticAlgorithm::setNewGeneration()
{
	int spacesAvailableInPopulation = populationAmount - population.size();
	// Fill the available spaces with the best of the worst individuals.
	// std::cout << "spacesAvailableInPopulation: " << spacesAvailableInPopulation << std::endl;
	// std::cout << "individuals left in ranked population: " << rankedPopulation.size() << std::endl; 
	for (int wheelCount = 0; wheelCount < spacesAvailableInPopulation; wheelCount++)
	{
		pushToPopulation(rankedPopulation.top());
		rankedPopulation.pop();
	}

	// Kill the worst individuals.
	while (!rankedPopulation.empty())
	{
		rankedPopulation.pop();
	}

	// Rank the new generation
	while (!this->population.empty())
	{
		Wheel * wheel = this->population.front();
		this->rankedPopulation.push(wheel);
		this->population.pop();
	}
	//rankedPopulation = priority_queue <Vehicle*>(); //Evitar el while, pero no se si afecta la memoria.
}


void GeneticAlgorithm::pushToPopulation(Wheel * pWheel)
{
	int configurationId = pWheel->getTreadId()*10 + pWheel->getTorqueId();
	this->frequencyTable[configurationId] += 1; 
	this->population.push(pWheel);
}

// SELECCION ORDENADA DE LOS MEJORES
void GeneticAlgorithm::evolve()
{
	std::cout << "Generation: " << this->generation << std::endl;
	std::queue<Wheel*> fittest = this->selectFittestParents();

	while (!fittest.empty())
	{
		Wheel * mom = fittest.front();
		fittest.pop();
		Wheel * dad = fittest.front();
		fittest.pop();
		Wheel* child = crossover(mom,dad);

		tryMutation(child);
		setIndividualFitness(child);
		pushToPopulation(child);
		pushToPopulation(mom);
		pushToPopulation(dad);
	}
	setNewGeneration();
	this->generation++;
}

void GeneticAlgorithm::start()
{
	this->consumer = std::thread(&GeneticAlgorithm::startEvolution, this);
}

void GeneticAlgorithm::join()
{
	this->consumer.join();
}
