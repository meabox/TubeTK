/*=========================================================================

Library:   TubeTK

Copyright 2010 Kitware Inc. 28 Corporate Drive,
Clifton Park, NY, 12065, USA.

All rights reserved.

Licensed under the Apache License, Version 2.0 ( the "License" );
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

=========================================================================*/


#include <stdio.h>
#include <ctype.h>
#include <iostream>
#include <fstream>
#include <string>

#include <metaUtils.h>
#include "MetaObjectDocument.h"

namespace tube {


const char * MetaObjectDocument::LABEL_NOBJECTS   = "NumberOfObjects";
const char * MetaObjectDocument::LABEL_TYPE       = "Type";
const char * MetaObjectDocument::LABEL_NAME       = "Name";
const char * MetaObjectDocument::LABEL_NUM_TRANS  = "NumberOfTransforms";
const char * MetaObjectDocument::LABEL_TRANSFORM  = "Transform";


/** Object Types *** NOTE:  MUST match the corresponding object values */
const char * MetaObjectDocument::ID_LABEL_BLOBTYPE  = "Blob";
const char * MetaObjectDocument::ID_LABEL_IMAGETYPE = "Image";
const char * MetaObjectDocument::ID_LABEL_SPATIALOBJTYPE = "SpatialObject";


MetaObjectDocument::
MetaObjectDocument() : m_MaxNumTransforms( 20 )
{
  m_NObjects = 0;
  if(META_DEBUG)
    {
    std::cout << "MetaObjectDocument()" << std::endl;
    }
  Clear();
}


MetaObjectDocument::
~MetaObjectDocument()
{
}


void MetaObjectDocument::
PrintInfo() const
{
  MetaDocument::PrintInfo();
  ObjectListType::const_iterator  it = m_objects.begin();
  int index = 1;
  while( it != m_objects.end() )
    {
    std::cout << "object Number: " << index <<std::endl;
    const char* name = (*it)->GetObjectName();
    int numTrans = (*it)->GetNumberOfTransforms();
    std::cout << "objectName = " << name << std::endl;
    std::cout << "NumberOfTransforms = " << numTrans << std::endl;
    std::cout << "\n";
    ++it;
    }
}


void MetaObjectDocument::
AddObject(ObjectDocumentType::Pointer object )
{
  m_objects.push_back( object );
  m_NObjects++;
}


void MetaObjectDocument::
SetObjectList( ObjectListType& list )
{
  m_objects = list;
  m_NObjects = static_cast<int>(list.size());
}


MetaObjectDocument::ObjectListType *
MetaObjectDocument::
GetObjectList(void)
{
  return &m_objects;
}


void MetaObjectDocument::
Clear(void)
{
  if(META_DEBUG) std::cout << "MetaObjectDocument: Clear" << std::endl;
  MetaDocument::Clear();

  m_objects.clear();
}


bool MetaObjectDocument::
Read( const char *_fileName )
{
  if(META_DEBUG)
    {
    std::cout << "MetaObjectDocument: Read" << std::endl;
    }

  if(_fileName != NULL)
    {
    strcpy(m_FileName, _fileName);
    }
  Clear();

  //Setup Document read fields and set Number of Objects read
  M_SetupReadFields();
  M_PrepareNewReadStream();

  m_ReadStream->open(m_FileName, std::ios::binary | std::ios::in);
  m_ReadStream->seekg(0,std::ios::beg);
  if(!m_ReadStream->is_open())
    {
    std::cout << "MetaObjectDocument: Read(): Cannot open file: "
      << m_FileName << std::endl;
    return false;
    }
  bool result = M_Read();

  m_ReadStream->close();
  m_ReadStream->clear();
  return result;
}


bool MetaObjectDocument::
Write(const char *_fileName)
{
  if(_fileName != NULL)
    {
    FileName(_fileName);
    }

  M_SetupWriteFields();

  if(!m_WriteStream)
    {
    m_WriteStream = new std::ofstream;
    }

#ifdef __sgi
  // Create the file. This is required on some older sgi's
  std::ofstream tFile(m_FileName,std::ios::out);
  tFile.close();
#endif
  m_WriteStream->open(m_FileName,std::ios::binary | std::ios::out);
  if(!m_WriteStream->is_open())
    {
    return false;
    }

  bool result = M_Write();

  m_WriteStream->close();
  delete m_WriteStream;
  m_WriteStream = 0;

  return result;
}


void MetaObjectDocument::
M_SetupReadFields(void)
{
  if(META_DEBUG)
    {
    std::cout << "MetaObjectDocument: M_SetupReadFields" << std::endl;
    }

  m_Fields.clear();

  MetaDocument::M_SetupReadFields();

  MET_FieldRecordType * mF;

  mF = new MET_FieldRecordType;
  MET_InitReadField(mF, LABEL_NOBJECTS, MET_INT, true);
  mF->required = true;
  mF->terminateRead = true;
  m_Fields.push_back(mF);
}


void MetaObjectDocument::
M_SetupWriteFields(void)
{
  m_Fields.clear();

  MetaDocument::M_SetupWriteFields();

  MET_FieldRecordType * mF;

  mF = new MET_FieldRecordType;
  MET_InitWriteField(mF, LABEL_NOBJECTS, MET_INT, m_NObjects);
  mF->required = true;
  m_Fields.push_back(mF);

  for( unsigned int i = 0; i < m_objects.size(); i++ )
    {
    M_SetupObjectWriteFields(i);
    }
}


void MetaObjectDocument::
M_SetupObjectReadFields(void)
{
  m_Fields.clear();
  MET_FieldRecordType * mF;

  std::string type;
  type.append( LABEL_TYPE );
  mF = new MET_FieldRecordType;
  MET_InitReadField(mF, type.c_str(), MET_STRING, true);
  m_Fields.push_back(mF);

  std::string object;
  object.append( LABEL_NAME );
  mF = new MET_FieldRecordType;
  MET_InitReadField(mF, object.c_str(), MET_STRING, false);
  m_Fields.push_back(mF);

  for( unsigned int i = 0; i < m_MaxNumTransforms; i++ )
    {
    std::string labelTransform;
    labelTransform.append( LABEL_TRANSFORM );
    char buffer[20];
    sprintf( buffer, "%u", i );
    labelTransform.append( buffer );

    mF = new MET_FieldRecordType;
    MET_InitReadField(mF, labelTransform.c_str(), MET_STRING, false);
    m_Fields.push_back(mF);
    }

  mF = new MET_FieldRecordType;
  MET_InitReadField(mF, "EndObject" , MET_NONE, true);
  mF->terminateRead = true;
  mF->required = true;
  m_Fields.push_back(mF);
}


void MetaObjectDocument::
M_SetupObjectWriteFields( unsigned int object_idx )
{
  MET_FieldRecordType * mF;

  //Record the type of object
  mF = new MET_FieldRecordType;
  MET_InitWriteField(mF, LABEL_TYPE, MET_STRING,
    strlen(m_objects[object_idx]->GetObjectType()),
    m_objects[object_idx]->GetObjectType() );
  m_Fields.push_back(mF);

  //Record the object Name
  mF = new MET_FieldRecordType;
  MET_InitWriteField(mF, LABEL_NAME, MET_STRING,
    strlen(m_objects[object_idx]->GetObjectName()),
    m_objects[object_idx]->GetObjectName() );
  m_Fields.push_back(mF);

  //Record Names of each Transform
  for( unsigned int i=0; i < m_objects[object_idx]->GetNumberOfTransforms(); i++ )
    {
    std::string label;
    label.append( LABEL_TRANSFORM );
    char buffer[20];
    sprintf( buffer, "%u", i );
    label.append( buffer );

    mF = new MET_FieldRecordType;
    MET_InitWriteField(mF, label.c_str(), MET_STRING,
      strlen(m_objects[object_idx]->GetTransformNames()[i]),
      m_objects[object_idx]->GetTransformNames()[i] );
    m_Fields.push_back(mF);
  }

  //Place end object tag
  mF = new MET_FieldRecordType;
  MET_InitWriteField(mF, "EndObject", MET_NONE );
  m_Fields.push_back(mF);
}


bool MetaObjectDocument::
M_Read(void)
{
  if(META_DEBUG)
    std::cout << "MetaObjectDocument: M_Read: Loading Header" << std::endl;

  if(!MetaDocument::M_Read())
    {
    std::cout << "MetaObjectDocument: M_Read: Error parsing file" << std::endl;
    return false;
    }

  if(META_DEBUG)
    std::cout << "MetaObjectDocument: M_Read: Parsing Header" << std::endl;

  MET_FieldRecordType * mF;

  mF = MET_GetFieldRecord(LABEL_NOBJECTS, &m_Fields);
  if(mF && mF->defined)
    {
    m_NObjects = (int)mF->value[0];
    }


  //Iterate through the objects
  for( int i = 0; i < m_NObjects; i++ )
    {
    //Setup the Object read fields
    M_SetupObjectReadFields();

    MET_Read(*m_ReadStream, & m_Fields, '=');

    ObjectDocumentType::Pointer object = ObjectDocumentType::New();

    mF = MET_GetFieldRecord( LABEL_TYPE, &m_Fields );
    if(mF->defined)
      {
      char * objectType = (char*)mF->value;
      if( !strcmp(objectType, ID_LABEL_IMAGETYPE) )
          {
          if(META_DEBUG)
            std::cout << "Reading in an image: " << objectType << std::endl;
          object = ImageDocumentType::New();
          }
        else if( !strcmp(objectType, ID_LABEL_BLOBTYPE) )
          {
          if(META_DEBUG)
            std::cout << "Reading in a blob: "  << objectType << std::endl;
          object = BlobSpatialObjectDocumentType::New();
          }
        else if( !strcmp(objectType, ID_LABEL_SPATIALOBJTYPE) )
          {
          if(META_DEBUG)
            std::cout << "Reading in a spatial object: "  << objectType << std::endl;
          object = SpatialObjectDocumentType::New();
          }
        else
          {
          std::cerr <<"Error: Object field type does not match any existing list of types for Object #" << i << std::endl;
          return false;
          }
      }

    //Read Object Name
    mF = MET_GetFieldRecord( LABEL_NAME, &m_Fields );
    if(mF->defined)
      {
      object->SetObjectName( (const char *) mF->value );
      }

    //Read Transform
    for( unsigned int j = 0; j < m_MaxNumTransforms; j++ )
      {
      std::string labelTransform;
      labelTransform.append( LABEL_TRANSFORM );
      char buffer[20];
      sprintf( buffer, "%u", j );
      labelTransform.append( buffer );

      mF = MET_GetFieldRecord( labelTransform.c_str(), &m_Fields );
      if(mF->defined)
        {
        // Vector reference count starts 1, not 0
        object->AddTransformNameToBack( (const char *) mF->value );
        const char* trans = (const char *) mF->value;
        if(META_DEBUG) std::cout <<" Transform : " << trans <<std::endl;
        }
      }
    m_objects.push_back( object );
    }
  return true;
}


bool MetaObjectDocument::
M_Write(void)
{
  if(!MetaDocument::M_Write())
    {
    std::cout << "MetaObjectDocument: M_Read: Error parsing file" << std::endl;
    return false;
    }

  std::ofstream fp;

  fp.open(m_FileName);
  if(!fp.is_open())
    {
    std::cout << "can't open file " << m_FileName << std::endl;
    return false;
    }

  if(!MET_Write(fp, &m_Fields))
    {
    std::cout << "MetaObject: Write: MET_Write Failed" << std::endl;
    return false;
    }

  fp.close();
  return true;
}

} // End namespace tube
