/*Copyright (c) 2017 The Paradox Game Converters Project

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.*/



#include "V2World.h"
#include <string>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <regex>
#include <list>
#include <queue>
#include <cmath>
#include <cfloat>
#include "ParadoxParser8859_15.h"
#include "ParadoxParserUTF8.h"
#include "Log.h"
#include "OSCompatibilityLayer.h"
#include "../Mappers/AdjacencyMapper.h"
#include "../Mappers/ContinentMapper.h"
#include "../Mappers/CountryMapping.h"
#include "../Mappers/CultureMapper.h"
#include "../Mappers/IdeaEffectMapper.h"
#include "../Mappers/MinorityPopMapper.h"
#include "../Mappers/ProvinceMapper.h"
#include "../Mappers/ReligionMapper.h"
#include "../Mappers/StateMapper.h"
#include "../Mappers/Vic2CultureUnionMapper.h"
#include "../Configuration.h"
#include "../EU4World/EU4World.h"
#include "../EU4World/EU4Relations.h"
#include "../EU4World/EU4Leader.h"
#include "../EU4World/EU4Province.h"
#include "../EU4World/EU4Diplomacy.h"
#include "V2Province.h"
#include "V2State.h"
#include "V2Relations.h"
#include "V2Army.h"
#include "V2Leader.h"
#include "V2Pop.h"
#include "V2Country.h"
#include "V2Reforms.h"
#include "V2Flags.h"
#include "V2LeaderTraits.h"



V2World::V2World(const EU4World& sourceWorld)
{
	LOG(LogLevel::Info) << "Parsing Vicky2 data";
	importProvinces();
	importDefaultPops();
	//logPopsByCountry();
	findCoastalProvinces();
	importPotentialCountries();
	importTechSchools();

	CountryMapping::createMappings(sourceWorld, potentialCountries);

	LOG(LogLevel::Info) << "Converting world";
	convertCountries(sourceWorld);
	convertProvinces(sourceWorld);
	convertDiplomacy(sourceWorld);
	setupColonies();
	setupStates();
	convertUncivReforms();
	convertTechs(sourceWorld);
	allocateFactories(sourceWorld);
	setupPops(sourceWorld);
	addUnions();
	convertArmies(sourceWorld);

	output();
}


void V2World::importProvinces()
{
	LOG(LogLevel::Info) << "Importing provinces";

	set<string> provinceFilenames = discoverProvinceFilenames();
	for (auto provinceFilename : provinceFilenames)
	{
		V2Province* newProvince = new V2Province(provinceFilename);
		provinces.insert(make_pair(newProvince->getNum(), newProvince));
	}

	if (Utils::DoesFileExist("./blankMod/output/localisation/text.csv"))
	{
		importProvinceLocalizations("./blankMod/output/localisation/text.csv");
	}
	else
	{
		importProvinceLocalizations((Configuration::getV2Path() + "/localisation/text.csv"));
	}
}


set<string> V2World::discoverProvinceFilenames()
{
	set<string> provinceFilenames;
	Utils::GetAllFilesInFolderRecursive("./blankMod/output/history/provinces", provinceFilenames);
	if (provinceFilenames.empty())
	{
		Utils::GetAllFilesInFolderRecursive(Configuration::getV2Path() + "/history/provinces", provinceFilenames);
	}

	return provinceFilenames;
}


void V2World::importProvinceLocalizations(const string& file)
{
	ifstream read(file);

	while (read.good() && !read.eof())
	{
		string line;
		getline(read, line);
		if (isAProvinceLocalization(line))
		{
			int position = line.find_first_of(';');
			int num = stoi(line.substr(4, position - 4));
			string name = line.substr(position + 1, line.find_first_of(';', position + 1) - position - 1);

			auto provice = provinces.find(num);
			if (provice != provinces.end())
			{
				provice->second->setName(name);
			}
		}
	}

	read.close();
}


bool V2World::isAProvinceLocalization(const string& line)
{
	return (line.substr(0, 4) == "PROV") && (isdigit(line[4]));
}


void V2World::importDefaultPops()
{
	LOG(LogLevel::Info) << "Importing historical pops.";

	totalWorldPopulation = 0;

	set<string> filenames;
	Utils::GetAllFilesInFolder("./blankMod/output/history/pops/1836.1.1/", filenames);
	for (auto filename : filenames)
	{
		importPopsFromFile(filename);
	}
}


void V2World::importPopsFromFile(const string& filename)
{
	list<int> popProvinces;

	Object* fileObj = parser_8859_15::doParseFile(("./blankMod/output/history/pops/1836.1.1/" + filename));
	vector<Object*> provinceObjs = fileObj->getLeaves();
	for (auto provinceObj : provinceObjs)
	{
		int provinceNum = stoi(provinceObj->getKey());
		popProvinces.push_back(provinceNum);

		importPopsFromProvince(provinceObj);
	}

	popRegions.insert(make_pair(filename, popProvinces));
}


void V2World::importPopsFromProvince(Object* provinceObj)
{
	int provinceNum = stoi(provinceObj->getKey());
	auto province = provinces.find(provinceNum);
	if (province == provinces.end())
	{
		LOG(LogLevel::Warning) << "Could not find province " << provinceNum << " for original pops.";
		return;
	}

	int provincePopulation = 0;
	int provinceSlavePopulation = 0;

	vector<Object*> popObjs = provinceObj->getLeaves();
	for (auto popObj: popObjs)
	{
		V2Pop* newPop = new V2Pop(popObj);

		province->second->addOldPop(newPop);
		if (minorityPopMapper::matchMinorityPop(newPop))
		{
			province->second->addMinorityPop(newPop);
		}

		totalWorldPopulation += newPop->getSize();
		provincePopulation += newPop->getSize();
		if (newPop->isSlavePop())
		{
			provinceSlavePopulation += newPop->getSize();
		}
	}

	province->second->setSlaveProportion(1.0 * provinceSlavePopulation / provincePopulation);
}


void V2World::logPopsByCountry() const
{
	map<string, map<string, long int>> popsByCountry; // country, poptype, num

	set<string> filenames;
	Utils::GetAllFilesInFolder("./blankMod/output/history/pops/1836.1.1/", filenames);
	for (auto filename : filenames)
	{
		logPopsFromFile(filename, popsByCountry);
	}

	outputLog(popsByCountry);
}


void V2World::logPopsFromFile(string filename, map<string, map<string, long int>>& popsByCountry) const
{
	Object* fileObj = parser_8859_15::doParseFile(("./blankMod/output/history/pops/1836.1.1/" + filename));

	vector<Object*> provinceObjs = fileObj->getLeaves();
	for (auto provinceObj : provinceObjs)
	{
		logPopsInProvince(provinceObj, popsByCountry);
	}
}


void V2World::logPopsInProvince(Object* provinceObj, map<string, map<string, long int>>& popsByCountry) const
{
	int provinceNum = stoi(provinceObj->getKey());
	auto province = provinces.find(provinceNum);
	if (province == provinces.end())
	{
		LOG(LogLevel::Warning) << "Could not find province " << provinceNum << " for original pops.";
		return;
	}

	auto countryPopItr = getCountryForPopLogging(province->second->getOwner(), popsByCountry);

	vector<Object*> pops = provinceObj->getLeaves();
	for (auto pop : pops)
	{
		logPop(pop, countryPopItr);
	}
}


void V2World::logPop(Object* pop, map<string, map<string, long int>>::iterator countryPopItr) const
{
	string popType = pop->getKey();
	int popSize = stoi(pop->getLeaf("size"));

	auto popItr = countryPopItr->second.find(pop->getKey());
	if (popItr == countryPopItr->second.end())
	{
		long int newPopSize = 0;
		pair<map<string, long int>::iterator, bool> newIterator = countryPopItr->second.insert(make_pair(popType, newPopSize));
		popItr = newIterator.first;
	}
	popItr->second += popSize;
}


map<string, map<string, long int>>::iterator V2World::getCountryForPopLogging(string country, map<string, map<string, long int>>& popsByCountry) const
{
	auto countryPopItr = popsByCountry.find(country);
	if (countryPopItr == popsByCountry.end())
	{
		map<string, long int> newCountryPop;
		auto newIterator = popsByCountry.insert(make_pair(country, newCountryPop));
		countryPopItr = newIterator.first;
	}

	return countryPopItr;
}


void V2World::outputLog(const map<string, map<string, long int>>& popsByCountry) const
{
	for (auto countryItr : popsByCountry)
	{
		long int total = 0;
		for (auto popsItr : countryItr.second)
		{
			total += popsItr.second;
		}

		for (auto popsItr : countryItr.second)
		{
			LOG(LogLevel::Info) << "," << countryItr.first << "," << popsItr.first << "," << popsItr.second << "," << static_cast<double>(popsItr.second / total);
		}

		LOG(LogLevel::Info) << "," << countryItr.first << "," << "Total," << total << "," << static_cast<double>(total / total);
	}
}


void V2World::findCoastalProvinces()
{
	LOG(LogLevel::Info) << "Finding coastal provinces.";
	Object* positionsObj = parser_8859_15::doParseFile((Configuration::getV2Path() + "/map/positions.txt"));
	if (positionsObj == nullptr)
	{
		LOG(LogLevel::Error) << "Could not parse file " << Configuration::getV2Path() << "/map/positions.txt";
		exit(-1);
	}

	vector<Object*> provinceObjs = positionsObj->getLeaves();
	for (auto provinceObj: provinceObjs)
	{
		determineIfProvinceIsCoastal(provinceObj);
	}
}


void V2World::determineIfProvinceIsCoastal(Object* provinceObj)
{
	vector<Object*> positionObj = provinceObj->getValue("building_position");
	if (positionObj.size() > 0)
	{
		vector<Object*> navalBaseObj = positionObj[0]->getValue("naval_base");
		if (navalBaseObj.size() > 0)
		{
			int provinceNum = stoi(provinceObj->getKey());
			auto province = provinces.find(provinceNum);
			if (province != provinces.end())
			{
				province->second->setCoastal(true);
			}
		}
	}
}


void V2World::importPotentialCountries()
{
	LOG(LogLevel::Info) << "Getting potential countries";
	potentialCountries.clear();
	dynamicCountries.clear();

	ifstream V2CountriesInput;
	V2CountriesInput.open("./blankMod/output/common/countries.txt");
	if (!V2CountriesInput.is_open())
	{
		LOG(LogLevel::Error) << "Could not open countries.txt. The converter may be corrupted, try downloading it again.";
		exit(-1);
	}

	bool dynamicSection = false;
	while (!V2CountriesInput.eof())
	{
		string line;
		getline(V2CountriesInput, line);

		if ((line[0] == '#') || (line.size() < 3))
		{
			continue;
		}
		else if (line.substr(0, 12) == "dynamic_tags")
		{
			dynamicSection = true;
			continue;
		}

		importPotentialCountry(line, dynamicSection);
	}

	V2CountriesInput.close();
}


void V2World::importPotentialCountry(const string& line, bool dynamicCountry)
{
	string tag = line.substr(0, 3);

	V2Country* newCountry = new V2Country(line, this, dynamicCountry);
	potentialCountries.insert(make_pair(tag, newCountry));
	if (dynamicCountry)
	{
		dynamicCountries.insert(make_pair(tag, newCountry));
	}
}


void V2World::importTechSchools()
{
	// Parse tech schools
	LOG(LogLevel::Info) << "Parsing tech schools.";
	Object* techSchoolObj = parser_UTF8::doParseFile("blocked_tech_schools.txt");
	if (techSchoolObj == NULL)
	{
		LOG(LogLevel::Error) << "Could not parse file blocked_tech_schools.txt";
		exit(-1);
	}
	vector<string> blockedTechSchools;	// the list of disallowed tech schools
	blockedTechSchools = initBlockedTechSchools(techSchoolObj);
	Object* technologyObj = parser_8859_15::doParseFile((Configuration::getV2Path() + "/common/technology.txt").c_str());
	if (technologyObj == NULL)
	{
		LOG(LogLevel::Error) << "Could not parse file " << Configuration::getV2Path() << "/common/technology.txt";
		exit(-1);
	}

	techSchools = initTechSchools(technologyObj, blockedTechSchools);
}

bool scoresSorter(pair<V2Country*, int> first, pair<V2Country*, int> second)
{
	return (first.second > second.second);
}


void V2World::convertCountries(const EU4World& sourceWorld)
{
	LOG(LogLevel::Info) << "Converting countries";
	const V2LeaderTraits lt;	// the V2 leader traits

	isRandomWorld = true;
	map<string, EU4Country*> sourceCountries = sourceWorld.getCountries();
	for (map<string, EU4Country*>::iterator i = sourceCountries.begin(); i != sourceCountries.end(); i++)
	{
		EU4Country* sourceCountry = i->second;
		if (i->first[0] != 'D' && sourceCountry->getRandomName().empty())
		{
			isRandomWorld = false;
		}

		std::string EU4Tag = sourceCountry->getTag();
		V2Country* destCountry = NULL;
		const std::string& V2Tag = CountryMapping::getVic2Tag(EU4Tag);
		if (!V2Tag.empty())
		{
			auto potentialCountry = potentialCountries.find(V2Tag);
			if (potentialCountry == potentialCountries.end())
			{ // No such V2 country exists yet for this tag so we make a new one.
				std::string countryFileName = sourceCountry->getName() + ".txt";
				destCountry = new V2Country(V2Tag, countryFileName, std::vector<V2Party*>(), this, true, false);
			}
			else
			{
				destCountry = potentialCountry->second;
			}
			destCountry->initFromEU4Country(sourceCountry, techSchools, leaderIDMap, lt);
			countries.insert(make_pair(V2Tag, destCountry));
		}
		else
		{
			LOG(LogLevel::Warning) << "Could not convert EU4 tag " << i->second->getTag() << " to V2";
		}
	}

	// set national values
	list< pair<V2Country*, int> > libertyScores;
	list< pair<V2Country*, int> > equalityScores;
	set<V2Country*>					valuesUnset;
	for (map<string, V2Country*>::iterator countryItr = countries.begin(); countryItr != countries.end(); countryItr++)
	{
		int libertyScore = 1;
		int equalityScore = 1;
		int orderScore = 1;
		countryItr->second->getNationalValueScores(libertyScore, equalityScore, orderScore);
		if (libertyScore > orderScore)
		{
			libertyScores.push_back(make_pair(countryItr->second, libertyScore));
		}
		if ((equalityScore > orderScore) && (equalityScore > libertyScore))
		{
			equalityScores.push_back(make_pair(countryItr->second, equalityScore));
		}
		valuesUnset.insert(countryItr->second);
		LOG(LogLevel::Debug) << "Value scores for " << countryItr->first << ": order = " << orderScore << ", liberty = " << libertyScore << ", equality = " << equalityScore;
	}
	equalityScores.sort(scoresSorter);
	int equalityLeft = 5;
	for (list< pair<V2Country*, int> >::iterator equalItr = equalityScores.begin(); equalItr != equalityScores.end(); equalItr++)
	{
		if (equalityLeft < 1)
		{
			break;
		}
		set<V2Country*>::iterator unsetItr = valuesUnset.find(equalItr->first);
		if (unsetItr != valuesUnset.end())
		{
			valuesUnset.erase(unsetItr);
			equalItr->first->setNationalValue("nv_equality");
			equalityLeft--;
			LOG(LogLevel::Debug) << equalItr->first->getTag() << " got national value equality";
		}
	}
	libertyScores.sort(scoresSorter);
	int libertyLeft = 20;
	for (list< pair<V2Country*, int> >::iterator libItr = libertyScores.begin(); libItr != libertyScores.end(); libItr++)
	{
		if (libertyLeft < 1)
		{
			break;
		}
		set<V2Country*>::iterator unsetItr = valuesUnset.find(libItr->first);
		if (unsetItr != valuesUnset.end())
		{
			valuesUnset.erase(unsetItr);
			libItr->first->setNationalValue("nv_liberty");
			libertyLeft--;
			LOG(LogLevel::Debug) << libItr->first->getTag() << " got national value liberty";
		}
	}
	for (set<V2Country*>::iterator unsetItr = valuesUnset.begin(); unsetItr != valuesUnset.end(); unsetItr++)
	{
		(*unsetItr)->setNationalValue("nv_order");
		LOG(LogLevel::Debug) << (*unsetItr)->getTag() << " got national value order";
	}

	// set prestige
	LOG(LogLevel::Debug) << "Setting prestige";
	double highestScore = 0.0;
	for (map<string, V2Country*>::iterator countryItr = countries.begin(); countryItr != countries.end(); countryItr++)
	{
		double score = 0.0;
		const EU4Country* srcCountry = countryItr->second->getSourceCountry();
		if (srcCountry != NULL)
		{
			score = srcCountry->getScore();
		}
		if (score > highestScore)
		{
			highestScore = score;
		}
	}
	for (map<string, V2Country*>::iterator countryItr = countries.begin(); countryItr != countries.end(); countryItr++)
	{
		double score = 0.0;
		const EU4Country* srcCountry = countryItr->second->getSourceCountry();
		if (srcCountry != NULL)
		{
			score = srcCountry->getScore();
		}
		double prestige = (score * 99.0 / highestScore) + 1;
		countryItr->second->addPrestige(prestige);
		LOG(LogLevel::Debug) << countryItr->first << " had " << prestige << " prestige";
	}

	// ALL potential countries should be output to the file, otherwise some things don't get initialized right
	for (auto potentialCountry : potentialCountries)
	{
		map<string, V2Country*>::iterator citr = countries.find(potentialCountry.first);
		if (citr == countries.end())
		{
			potentialCountry.second->initFromHistory();
			countries.insert(make_pair(potentialCountry.first, potentialCountry.second));
		}
	}

	checkForCivilizedNations();
}

void V2World::checkForCivilizedNations()
{
	unsigned int numCivilizedNations = 0;
	for (auto country : countries)
	{
		if (country.second->isCivilized())
		{
			numCivilizedNations++;
		}
	}

	if (numCivilizedNations < 8)
	{
		LOG(LogLevel::Warning) << "There were only " << numCivilizedNations << " civilized nations. You should mod the number of Great Powers to avoid crashes.";
	}
}

struct MTo1ProvinceComp
{
	vector<EU4Province*> provinces;
};

void V2World::convertProvinces(const EU4World& sourceWorld)
{
	LOG(LogLevel::Info) << "Converting provinces";

	for (auto Vic2Province : provinces)
	{
		auto EU4ProvinceNumbers = provinceMapper::getEU4ProvinceNumbers(Vic2Province.first);
		if (EU4ProvinceNumbers.size() == 0)
		{
			LOG(LogLevel::Warning) << "No source for " << Vic2Province.second->getName() << " (province " << Vic2Province.first << ')';
			continue;
		}
		else if (EU4ProvinceNumbers[0] == 0)
		{
			continue;
		}
		else if ((Configuration::getResetProvinces() == "yes") && provinceMapper::isProvinceResettable(Vic2Province.first))
		{
			Vic2Province.second->setResettable(true);
			continue;
		}

		Vic2Province.second->clearCores();

		EU4Province*	oldProvince = NULL;
		EU4Country*		oldOwner = NULL;
		// determine ownership by province count, or total population (if province count is tied)
		map<string, MTo1ProvinceComp> provinceBins;
		double newProvinceTotalBaseTax = 0;
		for (auto EU4ProvinceNumber : EU4ProvinceNumbers)
		{
			EU4Province* province = sourceWorld.getProvince(EU4ProvinceNumber);
			if (!province)
			{
				LOG(LogLevel::Warning) << "Old province " << EU4ProvinceNumber << " does not exist (bad mapping?)";
				continue;
			}
			EU4Country* owner = province->getOwner();
			string tag;
			if (owner != NULL)
			{
				tag = owner->getTag();
			}
			else
			{
				tag = "";
			}
			if (provinceBins.find(tag) == provinceBins.end())
			{
				provinceBins[tag] = MTo1ProvinceComp();
			}
			if (((Configuration::getV2Gametype() == "HOD") || (Configuration::getV2Gametype() == "HoD-NNM")) && false && (owner != NULL))
			{
				auto stateIndex = stateMapper::getStateIndex(Vic2Province.first);
				if (stateIndex == -1)
				{
					LOG(LogLevel::Warning) << "Could not find state index for province " << Vic2Province.first;
					continue;
				}
				else
				{
					map<int, set<string>>::iterator colony = colonies.find(stateIndex);
					if (colony == colonies.end())
					{
						set<string> countries;
						countries.insert(owner->getTag());
						colonies.insert(make_pair(stateIndex, countries));
					}
					else
					{
						colony->second.insert(owner->getTag());
					}
				}
			}
			else
			{
				provinceBins[tag].provinces.push_back(province);
				newProvinceTotalBaseTax += province->getBaseTax();
				// I am the new owner if there is no current owner, or I have more provinces than the current owner,
				// or I have the same number of provinces, but more population, than the current owner
				if (
					(oldOwner == NULL) ||
					(provinceBins[tag].provinces.size() > provinceBins[oldOwner->getTag()].provinces.size()) ||
					(provinceBins[tag].provinces.size() == provinceBins[oldOwner->getTag()].provinces.size())
					)
				{
					oldOwner = owner;
					oldProvince = province;
				}
			}
		}
		if (oldOwner == NULL)
		{
			Vic2Province.second->setOwner("");
			continue;
		}

		const std::string& V2Tag = CountryMapping::getVic2Tag(oldOwner->getTag());
		if (V2Tag.empty())
		{
			LOG(LogLevel::Warning) << "Could not map provinces owned by " << oldOwner->getTag();
		}
		else
		{
			Vic2Province.second->setOwner(V2Tag);
			map<string, V2Country*>::iterator ownerItr = countries.find(V2Tag);
			if (ownerItr != countries.end())
			{
				ownerItr->second->addProvince(Vic2Province.second);
			}
			Vic2Province.second->convertFromOldProvince(oldProvince);

			for (map<string, MTo1ProvinceComp>::iterator mitr = provinceBins.begin(); mitr != provinceBins.end(); ++mitr)
			{
				for (vector<EU4Province*>::iterator vitr = mitr->second.provinces.begin(); vitr != mitr->second.provinces.end(); ++vitr)
				{
					// assign cores
					vector<EU4Country*> oldCores = (*vitr)->getCores(sourceWorld.getCountries());
					for (vector<EU4Country*>::iterator j = oldCores.begin(); j != oldCores.end(); j++)
					{
						std::string coreEU4Tag = (*j)->getTag();
						// skip this core if the country is the owner of the EU4 province but not the V2 province
						// (i.e. "avoid boundary conflicts that didn't exist in EU4").
						// this country may still get core via a province that DID belong to the current V2 owner
						if ((coreEU4Tag == mitr->first) && (coreEU4Tag != oldOwner->getTag()))
						{
							continue;
						}

						const std::string& coreV2Tag = CountryMapping::getVic2Tag(coreEU4Tag);
						if (!coreV2Tag.empty())
						{
							Vic2Province.second->addCore(coreV2Tag);
						}
					}

					// determine demographics
					double provPopRatio = (*vitr)->getBaseTax() / newProvinceTotalBaseTax;
					vector<V2Demographic> demographics = determineDemographics((*vitr)->getPopRatios(), *vitr, Vic2Province.second, oldOwner, Vic2Province.first, provPopRatio);
					for (auto demographic : demographics)
					{
						Vic2Province.second->addPopDemographic(demographic);
					}

					// set forts and naval bases
					if ((*vitr)->hasBuilding("fort4") || (*vitr)->hasBuilding("fort5") || (*vitr)->hasBuilding("fort6"))
					{
						Vic2Province.second->setFortLevel(1);
					}
				}
			}
		}
	}
}

vector<V2Demographic> V2World::determineDemographics(vector<EU4PopRatio>& popRatios, EU4Province* eProv, V2Province* vProv, EU4Country* oldOwner, int destNum, double provPopRatio)
{
	vector<V2Demographic> demographics;
	for (auto prItr : popRatios)
	{
		string dstCulture = "no_culture";
		bool matched = cultureMapper::cultureMatch(prItr.culture, dstCulture, prItr.religion, eProv->getNum(), oldOwner->getTag());
		if (!matched)
		{
			LOG(LogLevel::Warning) << "Could not set culture for pops in Vic2 province " << destNum;
		}

		string religion = religionMapper::getVic2Religion(prItr.religion);;
		if (religion == "")
		{
			LOG(LogLevel::Warning) << "Could not set religion for pops in Vic2 province " << destNum;
		}

		string slaveCulture = "";
		matched = cultureMapper::slaveCultureMatch(prItr.culture, slaveCulture, prItr.religion, eProv->getNum(), oldOwner->getTag());;
		if (!matched)
		{
			//LOG(LogLevel::Warning) << "Could not set slave culture for pops in Vic2 province " << destNum;
			slaveCulture = "african_minor";
		}

		V2Demographic demographic;
		demographic.culture = dstCulture;
		demographic.slaveCulture = slaveCulture;
		demographic.religion = religion;
		demographic.upperRatio = prItr.upperPopRatio	* provPopRatio;
		demographic.middleRatio = prItr.middlePopRatio	* provPopRatio;
		demographic.lowerRatio = prItr.lowerPopRatio	* provPopRatio;
		demographic.oldCountry = oldOwner;
		demographic.oldProvince = eProv;

		//LOG(LogLevel::Info) << "EU4 Province " << eProv->getNum() << ", Vic2 Province " << vProv->getNum() << ", Culture: " << culture << ", Religion: " << religion << ", upperPopRatio: " << prItr.upperPopRatio << ", middlePopRatio: " << prItr.middlePopRatio << ", lowerPopRatio: " << prItr.lowerPopRatio << ", provPopRatio: " << provPopRatio << ", upperRatio: " << demographic.upperRatio << ", middleRatio: " << demographic.middleRatio << ", lowerRatio: " << demographic.lowerRatio;
		demographics.push_back(demographic);
	}

	return demographics;
}

void V2World::convertDiplomacy(const EU4World& sourceWorld)
{
	LOG(LogLevel::Info) << "Converting diplomacy";

	vector<EU4Agreement> agreements = sourceWorld.getDiplomaticAgreements();
	for (vector<EU4Agreement>::iterator itr = agreements.begin(); itr != agreements.end(); ++itr)
	{
		const std::string& EU4Tag1 = itr->country1;
		const std::string& V2Tag1 = CountryMapping::getVic2Tag(EU4Tag1);
		if (V2Tag1.empty())
		{
			continue;
		}
		const std::string& EU4Tag2 = itr->country2;
		const std::string& V2Tag2 = CountryMapping::getVic2Tag(EU4Tag2);
		if (V2Tag2.empty())
		{
			continue;
		}

		map<string, V2Country*>::iterator country1 = countries.find(V2Tag1);
		map<string, V2Country*>::iterator country2 = countries.find(V2Tag2);
		if (country1 == countries.end())
		{
			LOG(LogLevel::Warning) << "Vic2 country " << V2Tag1 << " used in diplomatic agreement doesn't exist";
			continue;
		}
		if (country2 == countries.end())
		{
			LOG(LogLevel::Warning) << "Vic2 country " << V2Tag2 << " used in diplomatic agreement doesn't exist";
			continue;
		}
		V2Relations* r1 = country1->second->getRelations(V2Tag2);
		if (!r1)
		{
			r1 = new V2Relations(V2Tag2);
			country1->second->addRelation(r1);
		}
		V2Relations* r2 = country2->second->getRelations(V2Tag1);
		if (!r2)
		{
			r2 = new V2Relations(V2Tag1);
			country2->second->addRelation(r2);
		}

		if (itr->type == "is_colonial")
		{
			country2->second->setColonyOverlord(country1->second);

			if (country2->second->getSourceCountry()->getLibertyDesire() < Configuration::getLibertyThreshold())
			{
				country1->second->absorbVassal(country2->second);
				for (vector<EU4Agreement>::iterator itr2 = agreements.begin(); itr2 != agreements.end(); ++itr2)
				{
					if (itr2->country2 == country2->second->getSourceCountry()->getTag())
					{
						itr2->country2 == country1->second->getSourceCountry()->getTag();
					}
				}
			}
			else
			{
				V2Agreement v2a;
				v2a.country1 = V2Tag1;
				v2a.country2 = V2Tag2;
				v2a.start_date = itr->startDate;
				v2a.type = "vassal";
				diplomacy.addAgreement(v2a);
				r1->setLevel(5);
			}
		}

		if (itr->type == "is_march")
		{
			country1->second->absorbVassal(country2->second);
			for (vector<EU4Agreement>::iterator itr2 = agreements.begin(); itr2 != agreements.end(); ++itr2)
			{
				if (itr2->country1 == country2->second->getSourceCountry()->getTag())
				{
					itr2->country1 = country1->second->getSourceCountry()->getTag();
				}
			}
		}

		if ((itr->type == "royal_marriage") || (itr->type == "guarantee"))
		{
			// influence level +1, but never exceed 4
			if (r1->getLevel() < 4)
			{
				r1->setLevel(r1->getLevel() + 1);
			}
		}
		if (itr->type == "royal_marriage")
		{
			// royal marriage is bidirectional; influence level +1, but never exceed 4
			if (r2->getLevel() < 4)
			{
				r2->setLevel(r2->getLevel() + 1);
			}
		}
		if ((itr->type == "vassal") || (itr->type == "union") || (itr->type == "protectorate"))
		{
			// influence level = 5
			r1->setLevel(5);
			/* FIXME: is this desirable?
			// if relations are too poor, country2 will immediately eject country1's ambassadors at the start of the game
			// so, for stability's sake, give their relations a boost
			if (r1->getRelations() < 1)
			r1->setRelations(1);
			if (r2->getRelations() < 1)
			r2->setRelations(1);
			*/
		}
		if ((itr->type == "alliance") || (itr->type == "vassal") || (itr->type == "union") || (itr->type == "guarantee"))
		{
			// copy agreement
			V2Agreement v2a;
			v2a.country1 = V2Tag1;
			v2a.country2 = V2Tag2;
			v2a.start_date = itr->startDate;
			v2a.type = itr->type;
			diplomacy.addAgreement(v2a);
		}
	}
}

void V2World::setupColonies()
{
	LOG(LogLevel::Info) << "Setting colonies";

	for (map<string, V2Country*>::iterator countryItr = countries.begin(); countryItr != countries.end(); countryItr++)
	{
		// find all land connections to capitals
		map<int, V2Province*>	openProvinces = provinces;
		queue<int>					goodProvinces;

		map<int, V2Province*>::iterator openItr = openProvinces.find(countryItr->second->getCapital());
		if (openItr == openProvinces.end())
		{
			continue;
		}
		if (openItr->second->getOwner() != countryItr->first) // if the capital is not owned, don't bother running
		{
			continue;
		}
		openItr->second->setLandConnection(true);
		goodProvinces.push(openItr->first);
		openProvinces.erase(openItr);

		do
		{
			int currentProvince = goodProvinces.front();
			goodProvinces.pop();
			vector<int> adjacencies = adjacencyMapper::getVic2Adjacencies(currentProvince);
			for (unsigned int i = 0; i < adjacencies.size(); i++)
			{
				map<int, V2Province*>::iterator openItr = openProvinces.find(adjacencies[i]);
				if (openItr == openProvinces.end())
				{
					continue;
				}
				if (openItr->second->getOwner() != countryItr->first)
				{
					continue;
				}
				openItr->second->setLandConnection(true);
				goodProvinces.push(openItr->first);
				openProvinces.erase(openItr);
			}
		} while (goodProvinces.size() > 0);

		// find all provinces on the same continent as the owner's capital
		string capitalContinent = "";
		map<int, V2Province*>::iterator capital = provinces.find(countryItr->second->getCapital());
		if (capital != provinces.end())
		{
			const EU4Province* capitalSrcProv = capital->second->getSrcProvince();
			if (!capitalSrcProv)
				continue;

			int capitalSrc = capitalSrcProv->getNum();
			capitalContinent = continentMapper::getEU4Continent(capitalSrc);
			if (capitalContinent == "")
			{
				continue;
			}
		}
		else
		{
			continue;
		}
		auto ownedProvinces = countryItr->second->getProvinces();
		for (auto provItr = ownedProvinces.begin(); provItr != ownedProvinces.end(); provItr++)
		{
			const EU4Province* provSrcProv = provItr->second->getSrcProvince();
			if (!provSrcProv)
				continue;

			int provSrc = provSrcProv->getNum();
			string continent = continentMapper::getEU4Continent(provSrc);
			if ((continent != "") && (continent == capitalContinent))
			{
				provItr->second->setSameContinent(true);
			}
		}
	}

	for (map<int, V2Province*>::iterator provItr = provinces.begin(); provItr != provinces.end(); provItr++)
	{
		provItr->second->determineColonial();
	}
}

static int stateId = 0;
void V2World::setupStates()
{
	LOG(LogLevel::Info) << "Creating states";
	list<V2Province*> unassignedProvs;
	for (map<int, V2Province*>::iterator itr = provinces.begin(); itr != provinces.end(); ++itr)
	{
		unassignedProvs.push_back(itr->second);
	}
	LOG(LogLevel::Debug) << "Unassigned Provs:\t" << unassignedProvs.size();

	list<V2Province*>::iterator iter;
	while (unassignedProvs.size() > 0)
	{
		iter = unassignedProvs.begin();
		int		provId = (*iter)->getNum();
		string	owner = (*iter)->getOwner();

		if (owner == "")
		{
			unassignedProvs.erase(iter);
			continue;
		}

		V2State* newState = new V2State(stateId, *iter);
		stateId++;
		vector<int> neighbors = stateMapper::getOtherProvincesInState(provId);

		LOG(LogLevel::Debug) << "Neighbors size" << neighbors.size();

		bool colonial = (*iter)->isColonial();
		newState->setColonial(colonial);
		iter = unassignedProvs.erase(iter);

		for (vector<int>::iterator i = neighbors.begin(); i != neighbors.end(); i++)
		{
			for (iter = unassignedProvs.begin(); iter != unassignedProvs.end(); iter++)
			{
				if ((*iter)->getNum() == *i)
				{
					if ((*iter)->getOwner() == owner)
					{
						if ((*iter)->isColonial() == colonial)
						{
							newState->addProvince(*iter);
							LOG(LogLevel::Debug) << (*iter)->getName() << " added to " << newState->getID();
							iter = unassignedProvs.erase(iter);
						}
					}
				}
			}
		}
		newState->colloectNavalBase();
		map<string, V2Country*>::iterator iter2 = countries.find(owner);
		if (iter2 != countries.end())
		{
			iter2->second->addState(newState);
		}
	}
}

void V2World::convertUncivReforms()
{
	LOG(LogLevel::Info) << "Setting unciv reforms";

	for (map<string, V2Country*>::iterator itr = countries.begin(); itr != countries.end(); ++itr)
	{
		itr->second->convertUncivReforms();
	}
}

void V2World::convertTechs(const EU4World& sourceWorld)
{
	LOG(LogLevel::Info) << "Converting techs";

	map<string, EU4Country*> sourceCountries = sourceWorld.getCountries();

	// Helper functions
	auto getCountryArmyTech = [&](EU4Country* country)
	{
		return country->getMilTech() + country->getAdmTech() + ideaEffectMapper::getArmyTechFromIdeas(country->getNationalIdeas());
	};

	auto getCountryNavyTech = [&](EU4Country* country)
	{
		return country->getMilTech() + country->getDipTech() + ideaEffectMapper::getNavyTechFromIdeas(country->getNationalIdeas());
	};

	auto getCountryCommerceTech = [&](EU4Country* country)
	{
		return country->getAdmTech() + country->getDipTech() + ideaEffectMapper::getCommerceTechFromIdeas(country->getNationalIdeas());
	};

	auto getCountryCultureTech = [&](EU4Country* country)
	{
		return country->getDipTech() + ideaEffectMapper::getCultureTechFromIdeas(country->getNationalIdeas());
	};

	auto getCountryIndustryTech = [&](EU4Country* country)
	{
		return country->getAdmTech() + country->getDipTech() + country->getMilTech() + ideaEffectMapper::getIndustryTechFromIdeas(country->getNationalIdeas());
	};

	double armyMax, armyMean;
	double navyMax, navyMean;
	double commerceMax, commerceMean;
	double cultureMax, cultureMean;
	double industryMax, industryMean;

	map<string, EU4Country*>::iterator i = sourceCountries.begin();
	while (i->second->getProvinces().size() == 0)
		i++;

	// Take mean and max from the first country
	EU4Country* currCountry = i->second;
	armyMax = armyMean = getCountryArmyTech(currCountry);
	navyMax = navyMean = getCountryNavyTech(currCountry);
	commerceMax = commerceMean = getCountryCommerceTech(currCountry);
	cultureMax = cultureMean = getCountryCultureTech(currCountry);
	industryMax = industryMean = getCountryIndustryTech(currCountry);

	int num = 2;

	// Helper for updating max and mean
	auto updateMeanMax = [&](double& max, double& mean, double techLevel)
	{
		if (techLevel > max)
			max = techLevel;
		mean = mean + (techLevel - mean) / num;
	};

	// Calculate max and mean
	for (i++; i != sourceCountries.end(); i++)
	{
		currCountry = i->second;
		if (currCountry->getProvinces().size() == 0)
			continue;

		updateMeanMax(armyMax, armyMean, getCountryArmyTech(currCountry));
		updateMeanMax(navyMax, navyMean, getCountryNavyTech(currCountry));
		updateMeanMax(commerceMax, commerceMean, getCountryCommerceTech(currCountry));
		updateMeanMax(cultureMax, cultureMean, getCountryCultureTech(currCountry));
		updateMeanMax(industryMax, industryMean, getCountryIndustryTech(currCountry));
		num++;
	}

	// Helper to normalize the score
	auto getNormalizedScore = [](double score, double max, double mean)
	{
		if (mean == max)
			return max;
		return (score - mean) / (max - mean);
	};

	// Set tech levels from normalized scores
	for (map<string, V2Country*>::iterator itr = countries.begin(); itr != countries.end(); itr++)
	{
		V2Country* country = itr->second;
		if ((Configuration::getV2Gametype() != "vanilla") && !country->isCivilized())
			continue;

		EU4Country* srcCountry = country->getSourceCountry();
		if (!srcCountry)
			continue;

		country->setArmyTech(getNormalizedScore(getCountryArmyTech(srcCountry), armyMax, armyMean));
		country->setNavyTech(getNormalizedScore(getCountryNavyTech(srcCountry), navyMax, navyMean));
		country->setCommerceTech(getNormalizedScore(getCountryCommerceTech(srcCountry), commerceMax, commerceMean));
		country->setCultureTech(getNormalizedScore(getCountryCultureTech(srcCountry), cultureMax, cultureMean));
		country->setIndustryTech(getNormalizedScore(getCountryIndustryTech(srcCountry), industryMax, industryMean));
	}
}

void V2World::allocateFactories(const EU4World& sourceWorld)
{
	// Construct factory factory
	LOG(LogLevel::Info) << "Determining factory allocation rules.";
	V2FactoryFactory factoryBuilder;

	LOG(LogLevel::Info) << "Allocating starting factories";

	// determine average production tech
	map<string, EU4Country*> sourceCountries = sourceWorld.getCountries();
	double admMean = 0.0f;
	int num = 1;
	for (map<string, EU4Country*>::iterator itr = sourceCountries.begin(); itr != sourceCountries.end(); ++itr)
	{
		if ((itr)->second->getProvinces().size() == 0)
		{
			continue;
		}
		if ((itr)->second->getTechGroup() != "western")
		{
			continue;
		}

		double admTech = (itr)->second->getAdmTech();
		admMean += ((admTech - admMean) / num);
		++num;
	}

	// give all extant civilized nations an industrial score
	deque<pair<double, V2Country*>> weightedCountries;
	for (map<string, V2Country*>::iterator itr = countries.begin(); itr != countries.end(); ++itr)
	{
		if (!itr->second->isCivilized())
		{
			continue;
		}

		const EU4Country* sourceCountry = itr->second->getSourceCountry();
		if (sourceCountry == NULL)
		{
			continue;
		}

		if (itr->second->getProvinces().size() == 0)
		{
			continue;
		}

		// modified manufactory weight follows diminishing returns curve y = x^(3/4)+log((x^2)/5+1)
		int manuCount = sourceCountry->getManufactoryCount();
		double manuWeight = pow(manuCount, 0.75) + log((manuCount * manuCount) / 5.0 + 1.0);
		double industryWeight = (sourceCountry->getAdmTech() - admMean) + manuWeight;
		// having one manufactory and average tech is not enough; you must have more than one, or above-average tech
		if (industryWeight > 1.0)
		{
			weightedCountries.push_back(pair<double, V2Country*>(industryWeight, itr->second));
		}
	}
	if (weightedCountries.size() < 1)
	{
		LOG(LogLevel::Warning) << "No countries are able to accept factories";
		return;
	}
	sort(weightedCountries.begin(), weightedCountries.end());

	// allow a maximum of 10 (plus any tied at tenth place) countries to recieve factories
	deque<pair<double, V2Country*>> restrictCountries;
	double threshold = 1.0;
	double totalIndWeight = 0.0;
	for (deque<pair<double, V2Country*>>::reverse_iterator itr = weightedCountries.rbegin(); itr != weightedCountries.rend(); ++itr)
	{
		if ((restrictCountries.size() > 10) && (itr->first < (threshold - FLT_EPSILON)))
		{
			break;
		}
		restrictCountries.push_front(*itr); // preserve sort
		totalIndWeight += itr->first;
		threshold = itr->first;
	}
	weightedCountries.swap(restrictCountries);

	// remove nations that won't have enough industiral score for even one factory
	deque<V2Factory*> factoryList = factoryBuilder.buildFactories();
	while (((weightedCountries.begin()->first / totalIndWeight) * factoryList.size() + 0.5 /*round*/) < 1.0)
	{
		weightedCountries.pop_front();
	}

	// determine how many factories each eligible nation gets
	vector<pair<int, V2Country*>> factoryCounts;
	for (deque<pair<double, V2Country*>>::iterator itr = weightedCountries.begin(); itr != weightedCountries.end(); ++itr)
	{
		int factories = int(((itr->first / totalIndWeight) * factoryList.size()) + 0.5 /*round*/);
		LOG(LogLevel::Debug) << itr->second->getTag() << " has industrial weight " << itr->first << " granting max " << factories << " factories";
		factoryCounts.push_back(pair<int, V2Country*>(factories, itr->second));
	}

	// allocate the factories
	vector<pair<int, V2Country*>>::iterator lastReceptiveCountry = factoryCounts.end()--;
	vector<pair<int, V2Country*>>::iterator citr = factoryCounts.begin();
	while (factoryList.size() > 0)
	{
		bool accepted = false;
		if (citr->first > 0) // can take more factories
		{
			for (deque<V2Factory*>::iterator qitr = factoryList.begin(); qitr != factoryList.end(); ++qitr)
			{
				if (citr->second->addFactory(*qitr))
				{
					--(citr->first);
					lastReceptiveCountry = citr;
					accepted = true;
					factoryList.erase(qitr);
					break;
				}
			}
		}
		if (!accepted && citr == lastReceptiveCountry)
		{
			Log logOutput(LogLevel::Debug);
			logOutput << "No countries will accept any of the remaining factories:\n";
			for (deque<V2Factory*>::iterator qitr = factoryList.begin(); qitr != factoryList.end(); ++qitr)
			{
				logOutput << "\t  " << (*qitr)->getTypeName() << '\n';
			}
			break;
		}
		if (++citr == factoryCounts.end())
		{
			citr = factoryCounts.begin(); // loop around to beginning
		}
	}
}

void V2World::setupPops(const EU4World& sourceWorld)
{
	LOG(LogLevel::Info) << "Creating pops";

	long		my_totalWorldPopulation = static_cast<long>(0.55 * totalWorldPopulation);
	double	popWeightRatio = my_totalWorldPopulation / sourceWorld.getWorldWeightSum();

	//ofstream output_file("Data.csv");

	int popAlgorithm = 0;
	if (*(sourceWorld.getVersion()) >= EU4Version("1.12.0"))
	{
		LOG(LogLevel::Info) << "Using pop conversion algorithm for EU4 versions after 1.12.";
		popAlgorithm = 2;
	}
	else
	{
		LOG(LogLevel::Info) << "Using pop conversion algorithm for EU4 versions prior to 1.12.";
		popAlgorithm = 1;
	}

	for (map<string, V2Country*>::iterator itr = countries.begin(); itr != countries.end(); ++itr)
	{
		itr->second->setupPops(popWeightRatio, popAlgorithm);
	}

	if (Configuration::getConvertPopTotals())
	{
		LOG(LogLevel::Info) << "Total world population: " << my_totalWorldPopulation;
	}
	else
	{
		LOG(LogLevel::Info) << "Total world population: " << totalWorldPopulation;
	}
	LOG(LogLevel::Info) << "Total world weight sum: " << sourceWorld.getWorldWeightSum();
	LOG(LogLevel::Info) << my_totalWorldPopulation << " / " << sourceWorld.getWorldWeightSum();
	LOG(LogLevel::Info) << "Population per weight point is: " << popWeightRatio;

	long newTotalPopulation = 0;
	// Heading
	/*output_file << "EU ID"		<< ",";
	output_file << "EU NAME"	<< ",";
	output_file << "OWNER"		<< ",";
	output_file << "BTAX"		<< ",";
	output_file << "TX INCOME"	<< ",";
	output_file << "PROD"		<< ",";
	output_file << "MP"			<< ",";
	output_file << "BUIDINGS"	<< ",";
	output_file << "TRADE"		<< ",";
	output_file << "TOTAL"		<< ",";
	output_file << "#DEST"		<< ",";
	output_file << "V2 ID"		<< ",";
	output_file << "V2 NAME"	<< ",";
	output_file << "CALC POPS"	<< ",";
	output_file << "POPS"		<< endl;*/
	for (auto itr = provinces.begin(); itr != provinces.end(); itr++)
	{
		// EU4ID, EU4Name, EU4TAG, BTX, TAX, PROD, MP, BUILD, TRADE, WEIGHT, DESTV2, V2Name, POPs //
		newTotalPopulation += itr->second->getTotalPopulation();

		//	EU4 Province ID
		//if (itr->second->getSrcProvince() != NULL)
		//{
		//	output_file << itr->second->getSrcProvince()->getNum() << ",";
		//}
		//else
		//{
		//	continue;
		//}
		////	EU4 Province Name
		//if (itr->second->getSrcProvince() != NULL)
		//{
		//	output_file << itr->second->getSrcProvince()->getProvName() << ",";
		//}
		//else
		//{
		//	output_file << "SEA" << ",";
		//}
		////	EU4 Province Owner
		//if (itr->second->getSrcProvince() != NULL)
		//{
		//	output_file << itr->second->getSrcProvince()->getOwnerString() << ",";
		//}
		//else
		//{
		//	output_file << "NULL" << ",";
		//}
		////	EU4 Base Tax
		//if (itr->second->getSrcProvince() != NULL)
		//{
		//	output_file << (2 * itr->second->getSrcProvince()->getBaseTax()) << ",";
		//}
		//else
		//{
		//	output_file << -1 << ",";
		//}
		////	EU4 Total Tax Income
		//if (itr->second->getSrcProvince() != NULL)
		//{
		//	output_file << 2*(itr->second->getSrcProvince()->getProvTaxIncome()) << ",";
		//}
		//else
		//{
		//	output_file << -1 << ",";
		//}
		////	EU4 Total Prod Income
		//if (itr->second->getSrcProvince() != NULL)
		//{
		//	output_file << itr->second->getSrcProvince()->getProvProdIncome() << ",";
		//}
		//else
		//{
		//	output_file << -1 << ",";
		//}
		////	EU4 Total Manpower weight
		//if (itr->second->getSrcProvince() != NULL)
		//{
		//	output_file << itr->second->getSrcProvince()->getProvMPWeight() << ",";
		//}
		//else
		//{
		//	output_file << -1 << ",";
		//}
		////	EU4 Total Building weight
		//if (itr->second->getSrcProvince() != NULL)
		//{
		//	output_file << itr->second->getSrcProvince()->getProvTotalBuildingWeight() << ",";
		//}
		//else
		//{
		//	output_file << -1 << ",";
		//}
		////	EU4 Total Tradegoods weight
		//if (itr->second->getSrcProvince() != NULL)
		//{
		//	output_file << itr->second->getSrcProvince()->getCurrTradeGoodWeight() << ",";
		//}
		//else
		//{
		//	output_file << -1 << ",";
		//}
		////	EU4 Province Weight
		//if (itr->second->getSrcProvince() != NULL)
		//{
		//	output_file << itr->second->getSrcProvince()->getTotalWeight() << ",";
		//}
		//else
		//{
		//	output_file << -1 << ",";
		//}
		////	Number of DestV2Provs
		//if (itr->second->getSrcProvince() != NULL)
		//{
		//	output_file << itr->second->getSrcProvince()->getNumDestV2Provs() << ",";
		//}
		//else
		//{
		//	output_file << -2 << ",";
		//}
		////	V2 Province ID
		//output_file << itr->second->getNum() << ",";
		////	V2 Province Name
		//if (itr->second->getName() == "")
		//{
		//	output_file << itr->second->getNum() << ",";
		//}
		//else
		//{
		//	output_file << itr->second->getName() << ",";
		//}
		////	Calculated V2 POPs
		//output_file << ((itr->second->getSrcProvince()->getTotalWeight()*popWeightRatio)/itr->second->getSrcProvince()->getNumDestV2Provs()) << ",";
		////	V2 POPs
		//output_file << itr->second->getTotalPopulation() << endl;
	}
	LOG(LogLevel::Info) << "New total world population: " << newTotalPopulation;

	//output_file.close();
}


void V2World::addUnions()
{
	LOG(LogLevel::Info) << "Adding unions";

	for (map<int, V2Province*>::iterator provItr = provinces.begin(); provItr != provinces.end(); provItr++)
	{
		if (!provItr->second->wasInfidelConquest() && !provItr->second->wasColony())
		{
			auto cultures = provItr->second->getCulturesOverThreshold(0.5);
			for (auto culture : cultures)
			{
				vector<string> cores = vic2CultureUnionMapper::getCoreForCulture(culture);
				for (auto core: cores)
				{
					provItr->second->addCore(core);
				}
			}
		}
	}
}


//#define TEST_V2_PROVINCES
void V2World::convertArmies(const EU4World& sourceWorld)
{
	LOG(LogLevel::Info) << "Converting armies and navies";

	// hack for naval bases.  not ALL naval bases are in port provinces, and if you spawn a navy at a naval base in
	// a non-port province, Vicky crashes....
	vector<int> port_whitelist;
	{
		int temp = 0;
		ifstream s("port_whitelist.txt");
		while (s.good() && !s.eof())
		{
			s >> temp;
			port_whitelist.push_back(temp);
		}
		s.close();
	}

	// get cost per regiment values
	double cost_per_regiment[num_reg_categories] = { 0.0 };
	Object*	obj2 = parser_8859_15::doParseFile("regiment_costs.txt");
	if (obj2 == NULL)
	{
		LOG(LogLevel::Error) << "Could not parse file regiment_costs.txt";
		exit(-1);
	}
	vector<Object*> objTop = obj2->getLeaves();
	if (objTop.size() == 0 || objTop[0]->getLeaves().size() == 0)
	{
		LOG(LogLevel::Error) << "regment_costs.txt failed to parse";
		exit(1);
	}
	for (int i = 0; i < num_reg_categories; ++i)
	{
		string regiment_cost = objTop[0]->getLeaf(RegimentCategoryNames[i]);
		cost_per_regiment[i] = atoi(regiment_cost.c_str());
	}

	// convert armies
	for (map<string, V2Country*>::iterator itr = countries.begin(); itr != countries.end(); ++itr)
	{
		itr->second->convertArmies(leaderIDMap, cost_per_regiment, provinces, port_whitelist);
	}
}

void V2World::output() const
{
	LOG(LogLevel::Info) << "Outputting mod";
	Utils::copyFolder("blankMod/output", "output/output");
	Utils::renameFolder("output/output", "output/" + Configuration::getOutputName());
	createModFile();

	// Create common\countries path.
	string countriesPath = "Output/" + Configuration::getOutputName() + "/common/countries";
	if (!Utils::TryCreateFolder(countriesPath))
	{
		return;
	}

	// Output common\countries.txt
	LOG(LogLevel::Debug) << "Writing countries file";
	FILE* allCountriesFile;
	if (fopen_s(&allCountriesFile, ("Output/" + Configuration::getOutputName() + "/common/countries.txt").c_str(), "w") != 0)
	{
		LOG(LogLevel::Error) << "Could not create countries file";
		exit(-1);
	}
	for (map<string, V2Country*>::const_iterator i = countries.begin(); i != countries.end(); i++)
	{
		const V2Country& country = *i->second;
		map<string, V2Country*>::const_iterator j = dynamicCountries.find(country.getTag());
		if (j == dynamicCountries.end())
		{
			country.outputToCommonCountriesFile(allCountriesFile);
		}
	}
	fprintf(allCountriesFile, "\n");
	if ((Configuration::getV2Gametype() == "HOD") || (Configuration::getV2Gametype() == "HoD_NNM"))
	{
		fprintf(allCountriesFile, "##HoD Dominions\n");
		fprintf(allCountriesFile, "dynamic_tags = yes # any tags after this is considered dynamic dominions\n");
		for (map<string, V2Country*>::const_iterator i = dynamicCountries.begin(); i != dynamicCountries.end(); i++)
		{
			i->second->outputToCommonCountriesFile(allCountriesFile);
		}
	}
	fclose(allCountriesFile);

	// Create flags for all new countries.
	V2Flags flags;
	flags.SetV2Tags(countries);
	flags.output();

	// Create localisations for all new countries. We don't actually know the names yet so we just use the tags as the names.
	LOG(LogLevel::Debug) << "Writing localisation text";
	string localisationPath = "Output/" + Configuration::getOutputName() + "/localisation";
	if (!Utils::TryCreateFolder(localisationPath))
	{
		return;
	}
	string source = Configuration::getV2Path() + "/localisation/text.csv";
	string dest = localisationPath + "/text.csv";

	if (isRandomWorld)
	{
		LOG(LogLevel::Debug) << "It's a random world";
		// we need to strip out the existing country names from the localisation file
		ifstream sourceFile(source);
		ofstream targetFile(dest);

		string line;
		std::regex countryTag("^[A-Z][A-Z][A-Z];");
		std::regex rebels("^REB;");
		std::smatch match;
		while (std::getline(sourceFile, line))
		{
			if (std::regex_search(line, match, countryTag) && !std::regex_search(line, match, rebels))
			{
				continue;
			}

			targetFile << line << '\n';
		}
		sourceFile.close();
		targetFile.close();

		// ...and also empty out 0_Names.csv
		FILE* zeronamesfile;
		string zeronamesfilepath = localisationPath + "/0_Names.csv";
		if (fopen_s(&zeronamesfile, zeronamesfilepath.c_str(), "w") != 0)
			fclose(zeronamesfile);
	}
	else
	{
		LOG(LogLevel::Debug) << "It's not a random world";
	}

	FILE* localisationFile;
	if (fopen_s(&localisationFile, (localisationPath + "/0_Names.csv").c_str(), "a") != 0)
	{
		LOG(LogLevel::Error) << "Could not update localisation text file";
		exit(-1);
	}

	for (map<string, V2Country*>::const_iterator i = countries.begin(); i != countries.end(); i++)
	{
		const V2Country& country = *i->second;
		if (country.isNewCountry())
		{
			country.outputLocalisation(localisationFile);
		}
	}
	fclose(localisationFile);

	LOG(LogLevel::Debug) << "Writing provinces";
	for (map<int, V2Province*>::const_iterator i = provinces.begin(); i != provinces.end(); i++)
	{
		i->second->output();
		LOG(LogLevel::Debug) << "province " << i->second->getName() << " have " << i->second->getNavalBaseLevel() << " naval base";	//test
	}
	LOG(LogLevel::Debug) << "Writing countries";
	for (map<string, V2Country*>::const_iterator itr = countries.begin(); itr != countries.end(); itr++)
	{
		itr->second->output();
	}
	diplomacy.output();

	outputPops();

	// verify countries got written
	ifstream V2CountriesInput;
	V2CountriesInput.open(("Output/" + Configuration::getOutputName() + "/common/countries.txt").c_str());
	if (!V2CountriesInput.is_open())
	{
		LOG(LogLevel::Error) << "Could not open countries.txt";
		exit(1);
	}

	bool	staticSection = true;
	while (!V2CountriesInput.eof())
	{
		string line;
		getline(V2CountriesInput, line);

		if ((line[0] == '#') || (line.size() < 3))
		{
			continue;
		}
		else if (line.substr(0, 12) == "dynamic_tags")
		{
			continue;
		}

		string countryFileName;
		int start = line.find_first_of('/');
		int size = line.find_last_of('\"') - start - 1;
		countryFileName = line.substr(start + 1, size);

		if (Utils::DoesFileExist("Output/" + Configuration::getOutputName() + "/common/countries/" + countryFileName))
		{
		}
		else if (Utils::DoesFileExist(Configuration::getV2Path() + "/common/countries/" + countryFileName))
		{
		}
		else
		{
			LOG(LogLevel::Warning) << "common/countries/" << countryFileName << " does not exists. This will likely crash Victoria 2.";
			continue;
		}
	}
	V2CountriesInput.close();
}

void V2World::createModFile() const
{
	ofstream modFile("Output/" + Configuration::getOutputName() + ".mod");
	if (!modFile.is_open())
	{
		LOG(LogLevel::Error) << "Could not create " << Configuration::getOutputName() << ".mod";
		exit(-1);
	}

	modFile << "name = \"Converted - " << Configuration::getOutputName() << "\"\n";
	modFile << "path = \"mod/" << Configuration::getOutputName() << "\"\n";
	modFile << "user_dir = \"" << Configuration::getOutputName() << "\"\n";
	modFile << "replace = \"history/provinces\"\n";
	modFile << "replace = \"history/countries\"\n";
	modFile << "replace = \"history/diplomacy\"\n";
	modFile << "replace = \"history/units\"\n";
	modFile << "replace = \"history/pops/1836.1.1\"\n";
	modFile << "replace = \"common/religion.txt\"\n";
	modFile << "replace = \"common/cultures.txt\"\n";
	modFile << "replace = \"common/countries.txt\"\n";
	modFile << "replace = \"common/countries/\"\n";
	modFile << "replace = \"gfx/interface/icon_religion.dds\"\n";
	modFile << "replace = \"localisation/0_Names.csv\"\n";
	modFile << "replace = \"localisation/0_Cultures.csv\"\n";
	modFile << "replace = \"localisation/0_Religions.csv\"\n";
	modFile << "replace = \"history/wars\"\n";

	modFile.close();
}

void V2World::outputPops() const
{
	LOG(LogLevel::Debug) << "Writing pops";
	for (auto popRegion : popRegions)
	{
		FILE* popsFile;
		if (fopen_s(&popsFile, ("Output/" + Configuration::getOutputName() + "/history/pops/1836.1.1/" + popRegion.first).c_str(), "w") != 0)
		{
			LOG(LogLevel::Error) << "Could not create pops file Output/" << Configuration::getOutputName() << "/history/pops/1836.1.1/" << popRegion.first;
			exit(-1);
		}

		for (auto provinceNumber : popRegion.second)
		{
			map<int, V2Province*>::const_iterator provItr = provinces.find(provinceNumber);
			if (provItr != provinces.end())
			{
				provItr->second->outputPops(popsFile);
			}
			else
			{
				LOG(LogLevel::Error) << "Could not find province " << provinceNumber << " while outputing pops!";
			}
		}
	}
}