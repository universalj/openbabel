/**********************************************************************
Copyright (C) 1998-2001 by OpenEye Scientific Software, Inc.
Portions Copyright (C) 2004 by Chris Morley

This file is part of the Open Babel project.
For more information, see <http://openbabel.sourceforge.net/>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation version 2 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
***********************************************************************/
#ifdef _WIN32
#pragma warning (disable : 4786)
#endif

#include <ctime>
#include <vector>
#include <map>
#include "mol.h"
#include "obconversion.h"
#include "obmolecformat.h"

using namespace std;
namespace OpenBabel {
class MOLFormat : public OBMoleculeFormat
{
public:
	//Register this format type ID
	MOLFormat() 
	{
		OBConversion::RegisterFormat("mol",this);
		OBConversion::RegisterFormat("mdl",this);
		OBConversion::RegisterFormat("sd",this);
		OBConversion::RegisterFormat("sdf",this);
	}

	virtual const char* Description()
	{ return
"MDL MOL format\n \
Reads and writes V2000 and V3000 versions\n \
Additional command line option for MOL files: -x[flags] (e.g. -x3)\n \
 2  output V2000 (default) or\n \
 3  output V3000 (used for >999 atoms or bonds) \n \
";
};

	virtual const char* SpecificationURL(){return
		"http://www.mdl.com/downloads/public/ctfile/ctfile.jsp";};

  virtual const char* GetMIMEType() 
  { return "chemical/x-mdl-molfile"; };

	virtual unsigned int Flags() { return DEFAULTFORMAT;};
	virtual const char* TargetClassDescription(){return OBMol::ClassDescription();};

	virtual int SkipObjects(int n, OBConversion* pConv)
	{
		if(n==0) n++;
		string temp;
		istream& ifs = *pConv->GetInStream();
		do
		{
			getline(ifs,temp,'$');
			if(ifs.good())
				getline(ifs, temp);
		}while(ifs.good() && temp.substr(0,3)!="$$$" && --n);
		return ifs.good() ? 1 : -1;	
	};

////////////////////////////////////////////////////
	/// The "API" interface functions
	virtual bool ReadMolecule(OBBase* pOb, OBConversion* pConv);
	virtual bool WriteMolecule(OBBase* pOb, OBConversion* pConv);

////////////////////////////////////////////////////
	//V3000 routines
private:
	bool ReadV3000Block(istream& ifs, OBMol& mol, OBConversion* pConv,bool DoMany);
	bool ReadV3000Line(istream& ifs, vector<string>& vs);
	bool ReadAtomBlock(istream& ifs,OBMol& mol, OBConversion* pConv);
	bool ReadBondBlock(istream& ifs,OBMol& mol, OBConversion* pConv);
	bool WriteV3000(ostream& ofs,OBMol& mol, OBConversion* pConv);
	char* GetTimeDate(char* td);
	map<int,int> indexmap; //relates index in file to index in OBMol
	vector<string> vs;
};

//Make an instance of the format class
MOLFormat theMOLFormat;

/////////////////////////////////////////////////////////////////
bool MOLFormat::ReadMolecule(OBBase* pOb, OBConversion* pConv)
{
	OBMol* pmol = dynamic_cast<OBMol*>(pOb);

	//Define some references so we can use the old parameter names
	//bool ReadSDFile(istream &ifs,OBMol &mol,const char *title)
	istream &ifs = *pConv->GetInStream();
	OBMol &mol = *pmol;

  int i,natoms,nbonds;
  char buffer[BUFF_SIZE];
  char *comment = NULL;
  string r1,r2;

  if (!ifs.getline(buffer,BUFF_SIZE)) return(false);
  mol.SetTitle(buffer);
  
	if (!ifs.getline(buffer,BUFF_SIZE)) return(false); //creator
  char* dimension = buffer+20;
	dimension[2]='\0'; //truncate after 2D
	if(strcmp(dimension,"2D") == 0 || strcmp(dimension,"3D") == 0)
	  pConv->SetDimension(dimension);
	if(strcmp(dimension,"2D") == 0)
	  mol.SetDimension(2);

	if (!ifs.getline(buffer,BUFF_SIZE)) return(false); //comment
  if (strlen(buffer) > 0) {
    comment = new char [strlen(buffer)+1];
    strcpy(comment,buffer);
  }

  if (!ifs.getline(buffer,BUFF_SIZE)) return(false); //atoms and bonds
  r1 = buffer;
  natoms = atoi((r1.substr(0,3)).c_str());
  nbonds = atoi((r1.substr(3,3)).c_str());

  mol.BeginModify();
	if(r1.find("V3000")!=string::npos)
	{
		indexmap.clear();
		if(ReadV3000Block(ifs,mol,pConv,false)) return false;
//		ifs.getline(buffer,BUFF_SIZE); //M END line
	}
	else
	{
		mol.ReserveAtoms(natoms);
		double x,y,z;
		char type[5];
		vector3 v;
		OBAtom atom;
		int charge;

		for (i = 0;i < natoms;i++) {
			if (!ifs.getline(buffer,BUFF_SIZE))
				return(false);

			if (sscanf(buffer,"%lf %lf %lf %s %*d %d",&x,&y,&z,type,&charge) != 5)
				return(false);
			v.SetX(x);v.SetY(y);v.SetZ(z);
			atom.SetVector(x, y, z);
			atom.SetAtomicNum(etab.GetAtomicNum(type));
			atom.SetType(type);

			switch (charge) {
			case 0: break;
			case 3: atom.SetFormalCharge(1); break;
			case 2: atom.SetFormalCharge(2); break;
			case 1: atom.SetFormalCharge(3); break;
			case 5: atom.SetFormalCharge(-1); break;
			case 6: atom.SetFormalCharge(-2); break;
			case 7: atom.SetFormalCharge(-3); break;
			}

			if (!mol.AddAtom(atom))
				return(false);
			atom.Clear();
		}

		int start,end,order,flag,stereo;
		for (i = 0;i < nbonds;i++) {
			flag = 0;
			if (!ifs.getline(buffer,BUFF_SIZE))
				return(false);
			r1 = buffer;
			start = atoi((r1.substr(0,3)).c_str());
			end = atoi((r1.substr(3,3)).c_str());
			order = atoi((r1.substr(6,3)).c_str());
			order = (order == 4) ? 5 : order;
			if (r1.size() >= 12) {  //handle wedge/hash data
				stereo = atoi((r1.substr(9,3)).c_str());
				if (stereo) {
					if (stereo == 1) flag |= OB_WEDGE_BOND;
					if (stereo == 6) flag |= OB_HASH_BOND;
				}
			}

			if (!mol.AddBond(start,end,order,flag)) return(false);
		}

		//CM start 18 Sept 2003
		//Read Properties block, currently only M RAD and M CHG 

		while(ifs.getline(buffer,BUFF_SIZE))
		{
			if(!strchr(buffer,'M')) continue;
			r1 = buffer;
			int n = atoi((r1.substr(6,3)).c_str()); //entries on this line
			if(n==0) break;
			int pos = 10;
			for(;n>0;n--,pos+=8)
			{
				int atomnumber = atoi((r1.substr(pos,3)).c_str());
				if (atomnumber==0) break;
				OBAtom* at;
				at=mol.GetAtom(atomnumber); //atom numbers start at 1
				int value = atoi((r1.substr(pos+4,3)).c_str());
				if(r1.substr(3,3)=="RAD")
					at->SetSpinMultiplicity(value);
				else if(r1.substr(3,3)=="CHG")
					at->SetFormalCharge(value);
				//Although not done here,according to the specification, 
				//previously set formal charges should be reset to zero
				// Lines setting several other properties are not implemented
			}
		}
	}
  mol.AssignSpinMultiplicity();

	mol.EndModify();

  if (comment)
  {
	  OBCommentData *cd = new OBCommentData;
	  mol.SetData(cd);
  }

  while (ifs.getline(buffer,BUFF_SIZE)) {
    // RWT 4/7/2001
    // added to get properties
    if (strstr(buffer,"<")) {
      string buff(buffer);
      size_t lt=buff.find("<")+1;
      size_t rt = buff.find_last_of(">");
      string attr = buff.substr(lt,rt-lt);
      ifs.getline(buffer,BUFF_SIZE);

	  OBPairData *dp = new OBPairData;
	  dp->SetAttribute(attr);
	  dp->SetValue(buffer);
      mol.SetData(dp);
    }
    // end RWT    

    if (!strncmp(buffer,"$$$$",4)) break;
    if (!strncmp(buffer,"$MOL",4)) break; //CM
  }
	delete comment;

  return(true);

}

/////////////////////////////////////////////////////////////////
bool MOLFormat::WriteMolecule(OBBase* pOb, OBConversion* pConv)
{
	OBMol* pmol = dynamic_cast<OBMol*>(pOb);

	//Define some references so we can use the old parameter names
	ostream &ofs = *pConv->GetOutStream();
	OBMol &mol = *pmol;
	const char *dimension = pConv->GetDimension();

	ofs << mol.GetTitle() <<  endl; //line 1

	char td[11];
	ofs << " OpenBabel" << GetTimeDate(td) <<  dimension << endl; //line2

	if (mol.HasData(obCommentData))
		{
		  OBCommentData *cd = (OBCommentData*)mol.GetData(obCommentData);
		  ofs << cd->GetData() << endl; //line 3
		}
	else
	  ofs << endl;
	
	if(pConv->IsOption('3') || mol.NumAtoms() > 999 || mol.NumBonds() > 999)
	  {
	    if(!WriteV3000(ofs,mol,pConv)) return false;
	  }

	else
	{
		//The rest of the function is the same as the original
		char buff[BUFF_SIZE];  

		if (mol.NumAtoms() > 999) // Three digits!
			{
				ThrowError("MDL Molfile conversion failed: Molecule is too large to convert.");
				ThrowError("  File format limited to 999 atoms.");
				cerr << "  Molecule size: " << mol.NumAtoms() << " atoms " << endl;
	//      delete pOb;
				return(false);
			}

		if (mol.NumBonds() > 999) // Three digits!
			{
				ThrowError("MDL Molfile conversion failed: Molecule is too large to convert.");
				ThrowError("  File format limited to 999 bonds.");
				cerr << "  Molecule size: " << mol.NumBonds() << " atoms " << endl;
	//      delete pOb;
				return(false);
			}

		sprintf(buff,"%3d%3d%3d%3d%3d%3d%3d%3d%3d%3d%3d",
						mol.NumAtoms(),mol.NumBonds(),0,0,0,0,0,0,0,0,1);// CM 18 Sept 2003 1 was 0 (# extra lines)
		ofs << buff << endl;

		OBAtom *atom;
		vector<OBNodeBase*>::iterator i;
		int charge;
		for (atom = mol.BeginAtom(i);atom;atom = mol.NextAtom(i)) {
			switch (atom->GetFormalCharge()) {
			case 1: charge = 3; break;
			case 2: charge = 2; break;
			case 3: charge = 1; break;
			case -1: charge = 5; break;
			case -2: charge = 6; break;
			case -3: charge = 7; break;
			default:
				charge=0; break;
			}

			sprintf(buff,"%10.4f%10.4f%10.4f %-3s%2d%3d%3d%3d%3d",
							atom->GetX(),
							atom->GetY(),
							atom->GetZ(),
							(etab.GetSymbol(atom->GetAtomicNum())),
							0,charge,0,0,0);    
			ofs << buff << endl;
		}

		//so the bonds come out sorted
		OBAtom *nbr;
		OBBond *bond;
		vector<OBEdgeBase*>::iterator j;
		for (atom = mol.BeginAtom(i);atom;atom = mol.NextAtom(i))
			for (nbr = atom->BeginNbrAtom(j);nbr;nbr = atom->NextNbrAtom(j))
				if (atom->GetIdx() < nbr->GetIdx()) {
					bond = (OBBond*) *j;
					
					int stereo=0; //21Jan05 CM
					if(strcmp(dimension,"2D")==0)
					{
						int flag = bond->GetFlags();
						if (flag & OB_WEDGE_BOND) stereo=1;
						if (flag & OB_HASH_BOND ) stereo=6;
					}
					sprintf(buff,"%3d%3d%3d%3d%3d%3d",
									bond->GetBeginAtomIdx(),
									bond->GetEndAtomIdx(),
									(bond->GetBO() == 5) ? 4 : bond->GetBO(),
									stereo,0,0);
					ofs << buff << endl;
				}

		//CM start 18 Sept 2003
		//For radicals
		char txt[50];
		*buff=0;
		int radcount=0;
		for (atom = mol.BeginAtom(i);atom;atom = mol.NextAtom(i))
		{
			if(atom->GetSpinMultiplicity())
			{
				sprintf(txt,"%3d %3d ",atom->GetIdx(),atom->GetSpinMultiplicity()); //radicals=>2 all carbenes=>3	
				strcat(buff,txt);
				radcount++;
			}
		}
		if (radcount)
		{
			sprintf(txt,"M  RAD%3d ",radcount);
			ofs << txt << buff << endl;
		}
		// CM end
	}

	ofs << "M  END" << endl;


  // RWT 4/7/2001
  // now output properties if they exist
  // MTS 4/17/2001
  // changed to use new OBGenericData class

  vector<OBGenericData*>::iterator k;
  vector<OBGenericData*> vdata = mol.GetData();
  for (k = vdata.begin();k != vdata.end();k++)
	  if ((*k)->GetDataType() == obPairData)
	  {
		  ofs << ">  <" << (*k)->GetAttribute() << ">" << endl;
		  ofs << ((OBPairData*)(*k))->GetValue() << endl << endl;
	  }

  // end RWT

	if(!pConv->IsLast())
		ofs << "$$$$" << endl;

	return(true);
}


//////////////////////////////////////////////////////
bool MOLFormat::ReadV3000Block(istream& ifs, OBMol& mol, OBConversion* pConv,bool DoMany)
{
	do
	{
		if(!ReadV3000Line(ifs,vs)) return false;
		if(vs[2]=="LINKNODE"){continue;} //not implemented
		if(vs[2]!="BEGIN") return false;

		if(vs[3]=="CTAB")
		{
			if(!ReadV3000Line(ifs,vs) || vs[2]!="COUNTS") return false;
			int natoms = atoi(vs[3].c_str());
			int nbonds = atoi(vs[4].c_str());
			int chiral = atoi(vs[7].c_str()); 
			//number of s groups, number of 3D contraints, chiral flag and regno not yet implemented
			mol.ReserveAtoms(natoms);

			ReadV3000Block(ifs,mol,pConv,true);//go for contained blocks	
			if(!ReadV3000Line(ifs,vs) || vs[2]!="END" || vs[3]!="CTAB") return false;
			return true;
		}
		else if(vs[3]=="ATOM")
			ReadAtomBlock(ifs,mol,pConv);
		else if(vs[3]=="BOND")
			ReadBondBlock(ifs,mol,pConv);
		/*
		else if(vs[3]=="COLLECTION")
			//not currently implemented
		else if(vs[3]=="3D")
			//not currently implemented
		else if(vs[3]=="SGROUP")
			//not currently implemented
		else if(vs[3]=="RGROUP")
			//not currently implemented
		*/
	}while(DoMany && ifs.good());
	return true;
}

//////////////////////////////////////////////////////
bool MOLFormat::ReadV3000Line(istream& ifs, vector<string>& vs)
{
  char buffer[BUFF_SIZE];
	if(!ifs.getline(buffer,BUFF_SIZE)) return false;
	tokenize(vs,buffer," \t\n\r");
	if(vs[0]!="M" || vs[1]!="V30") return false;
	
	if(buffer[strlen(buffer)-1] == '-') //continuation char
	{
		//Read continuation line iteratively and add parsed tokens (without M V30) to vs
		vector<string> vsx;
		if(!ReadV3000Line(ifs,vsx)) return false;
		vs.insert(vs.end(),vsx.begin()+3,vsx.end());
	}
	return true;
}

//////////////////////////////////////////////////////
bool MOLFormat::ReadAtomBlock(istream& ifs,OBMol& mol, OBConversion* pConv)
{	
	OBAtom atom;
	int obindex;
	for(obindex=1;;obindex++)
	{
		if(!ReadV3000Line(ifs,vs)) return false;
		if(vs[2]=="END") break;
		
    indexmap[atoi(vs[2].c_str())] = obindex;
		atom.SetVector(atof(vs[4].c_str()), atof(vs[5].c_str()), atof(vs[6].c_str()));
    
		char type[5];
		strncpy(type,vs[3].c_str(),4);
		atom.SetAtomicNum(etab.GetAtomicNum(type));
    atom.SetType(type); //takes a char not a const char!
		//mapping vs[7] not implemented
		
		//Atom properties
		vector<string>::iterator itr;
		for(itr=vs.begin()+8;itr!=vs.end();itr++)
		{
			int pos = (*itr).find('=');
			if (pos==string::npos) return false;
			int val = atoi((*itr).substr(pos+1).c_str());

			if((*itr).substr(0,pos)=="CHG")
			{
		   atom.SetFormalCharge(val);
			}
			else if((*itr).substr(0,pos)=="RAD")
			{
				atom.SetSpinMultiplicity(val);
			}
			else if((*itr).substr(0,pos)=="CFG")
			{
				//Stereo configuration: 0 none; 1 odd parity; 2 even parity; (3 either parity)
				if(val==1) atom.SetAntiClockwiseStereo();
				else if(val==2) atom.SetClockwiseStereo();
			}
			else if((*itr).substr(0,pos)=="MASS")
			{
				if(val) atom.SetIsotope(val);
			}
			else if((*itr).substr(0,pos)=="VAL")
			{
				//TODO Abnormal valence: 0 normal;-1 zero
			}
			//Several query properties unimplemented
			//Unknown properties ignored
		}
		if(!mol.AddAtom(atom)) return false;
    atom.Clear();
	}
	return true;
}

//////////////////////////////////////////////////////
bool MOLFormat::ReadBondBlock(istream& ifs,OBMol& mol, OBConversion* pConv)
{
	for(;;)
	{
		if(!ReadV3000Line(ifs,vs)) return false;
		if(vs[2]=="END") break;
		
		unsigned flag=0;

		int order = atoi(vs[3].c_str());
		if(order==4) order=5;

		int obstart = indexmap[atoi(vs[4].c_str())];
		int obend = indexmap[atoi(vs[5].c_str())];

		vector<string>::iterator itr;
		for(itr=vs.begin()+6;itr!=vs.end();itr++)
		{
			int pos = (*itr).find('=');
			if (pos==string::npos) return false;
			int val = atoi((*itr).substr(pos+1).c_str());

			if((*itr).substr(0,pos)=="CFG")
			{
		   //TODO Bond Configuration 2 or 3D??
				if (val == 1) 
				{
					flag |= OB_TORUP_BOND;
				}
				else if (val == 3) 
				{
					flag |= OB_TORDOWN_BOND;
				}
			}
		}
	  if (!mol.AddBond(obstart,obend,order,flag)) return false;
	}
	return true;
}

//////////////////////////////////////////////////////////
bool MOLFormat::WriteV3000(ostream& ofs,OBMol& mol, OBConversion* pConv)
{
	ofs << "  0  0  0     0  0            999 V3000" << endl; //line 4
	ofs << "M  V30 BEGIN CTAB" <<endl;
	ofs	<< "M  V30 COUNTS " << mol.NumAtoms() << " " << mol.NumBonds() 
			<< " 0 0 " << mol.IsChiral() << " 0" << endl;
	
	ofs << "M  V30 BEGIN ATOM" <<endl;
	OBAtom *atom;
	int index=1;
	vector<OBNodeBase*>::iterator i;
	for (atom = mol.BeginAtom(i);atom;atom = mol.NextAtom(i))
	{
		ofs	<< "M  V30 "
				<< index++ << " "
				<< etab.GetSymbol(atom->GetAtomicNum()) << " "
				<< atom->GetX() << " "
				<< atom->GetY() << " "
				<< atom->GetZ()
				<< " 0";
		if(atom->GetFormalCharge()!=0)
			ofs << " CHG=" << atom->GetFormalCharge();
		if(atom->GetSpinMultiplicity()!=0)
			ofs << " RAD=" << atom->GetSpinMultiplicity();
		if(atom->HasChiralitySpecified())
		{
			int cfg=0;
			if (atom->IsClockwise())cfg=1;
			else if(atom->IsAntiClockwise())cfg=2;
			ofs << " CFG=" << cfg;
		}
		if(atom->GetIsotope()!=0)
			ofs << " MASS=" << atom->GetIsotope();
		ofs << endl;
	}
	ofs << "M  V30 END ATOM" <<endl;

	ofs << "M  V30 BEGIN BOND" <<endl;
	//so the bonds come out sorted
	index=1;
	OBAtom *nbr;
	OBBond *bond;
	vector<OBEdgeBase*>::iterator j;
	for (atom = mol.BeginAtom(i);atom;atom = mol.NextAtom(i))
	{
		for (nbr = atom->BeginNbrAtom(j);nbr;nbr = atom->NextNbrAtom(j))
		{
			if (atom->GetIdx() < nbr->GetIdx())
			{
				bond = (OBBond*) *j;
				ofs << "M  V30 "
						<< index++ << " "
						<< ((bond->GetBO() == 5) ? 4 : bond->GetBO()) << " "
						<< bond->GetBeginAtomIdx() << " "
						<< bond->GetEndAtomIdx();
				//TODO do the following stereo chemistry properly
				int cfg=0;
				if(bond->IsWedge()) cfg=1;
				if(bond->IsHash()) cfg=3;
				if(cfg) ofs << " CFG=" << cfg;
				ofs << endl;
			}
		}
	}
	ofs << "M  V30 END BOND" <<endl;
	ofs << "M  V30 END CTAB" <<endl;
	return true;
}

char*	MOLFormat::GetTimeDate(char* td)
{
  //returns MMDDYYHHmm
	struct tm* ts;
  time_t long_time;
  time( &long_time );
	ts = localtime( &long_time ); 
	sprintf(td,"%02d%02d%02d%02d%02d", ts->tm_mon+1, ts->tm_mday, 
			((ts->tm_year>=100)? ts->tm_year-100 : ts->tm_year),
			ts->tm_hour, ts->tm_min);
	return td;
}

}
