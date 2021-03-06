/*=========================================================================

Library:   TubeTK

Copyright 2010 Kitware Inc. 28 Corporate Drive,
Clifton Park, NY, 12065, USA.

All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

=========================================================================*/

#include "itktubeImageToTubeRigidRegistration.h"
#include "itktubeRecordOptimizationParameterProgressionCommand.h"
#include "itktubeSubSampleTubeTreeSpatialObjectFilter.h"
#include "itktubeSubSampleTubeTreeSpatialObjectFilterSerializer.h"
#include "itktubeTubeAngleOfIncidenceWeightFunction.h"
#include "itktubeTubeAngleOfIncidenceWeightFunctionSerializer.h"
#include "itktubeTubeExponentialResolutionWeightFunction.h"
#include "itktubeTubePointWeightsCalculator.h"
#include "itktubeTubeToTubeTransformFilter.h"
#include "itkUltrasoundProbeGeometryCalculator.h"
#include "itkUltrasoundProbeGeometryCalculatorSerializer.h"
#include "tubeCLIFilterWatcher.h"
#include "tubeCLIProgressReporter.h"
#include "tubeMessage.h"

#include <itkJsonCppArchiver.h>
#include <itkGradientDescentOptimizerSerializer.h>

#include <itkImageFileReader.h>
#include <itkImageFileWriter.h>
#include <itkRecursiveGaussianImageFilter.h>
#include <itkSpatialObjectReader.h>
#include <itkTransformFileWriter.h>
#include <itkTimeProbesCollectorBase.h>

#include <json/writer.h>

#include "RegisterImageToTubesUsingRigidTransformCLP.h"

template< class TPixel, unsigned int VDimension >
int DoIt( int argc, char * argv[] );

// Does not currently use TPixel
#define PARSE_ARGS_FLOAT_ONLY 1

// Must follow include of "...CLP.h" and forward declaration of int DoIt( ... ).
#include "tubeCLIHelperFunctions.h"

template< class TPixel, unsigned int VDimension >
int DoIt( int argc, char * argv[] )
{
  PARSE_ARGS;

  // The timeCollector is used to perform basic profiling of the components
  //   of your algorithm.
  itk::TimeProbesCollectorBase timeCollector;

  // CLIProgressReporter is used to communicate progress with the Slicer GUI
  tube::CLIProgressReporter progressReporter(
    "RegisterImageToTubesUsingRigidTransform",
    CLPProcessInformation );
  progressReporter.Start();

#ifdef SlicerExecutionModel_USE_SERIALIZER
  // If SlicerExecutionModel was built with Serializer support, there is
  // automatically a parametersToRestore argument.  This argument is a JSON
  // file that has values for the CLI parameters, but it can also hold other
  // entries without causing any issues.
  Json::Value parametersRoot;
  if( !parametersToRestore.empty() )
    {
    // Parse the Json.
    std::ifstream stream( parametersToRestore.c_str() );
    Json::Reader reader;
    reader.parse( stream, parametersRoot );
    stream.close();
    }
#endif

  const unsigned int Dimension = 3;
  typedef double     FloatType;

  typedef itk::VesselTubeSpatialObject< Dimension >      TubeType;
  typedef itk::GroupSpatialObject< Dimension >           TubeNetType;
  typedef itk::SpatialObjectReader< Dimension >          TubeNetReaderType;
  typedef itk::Image< FloatType, Dimension >             ImageType;
  typedef itk::ImageFileReader< ImageType >              ImageReaderType;
  typedef itk::ImageFileWriter< ImageType >              ImageWriterType;
  typedef itk::tube::ImageToTubeRigidRegistration< ImageType, TubeNetType, TubeType >
                                                         RegistrationMethodType;
  typedef typename RegistrationMethodType::TransformType TransformType;
  typedef itk::tube::TubeToTubeTransformFilter< TransformType, Dimension >
                                                         TubeTransformFilterType;

  timeCollector.Start("Load data");
  typename ImageReaderType::Pointer reader = ImageReaderType::New();
  reader->SetFileName( inputVolume.c_str() );
  try
    {
    reader->Update();
    }
  catch( itk::ExceptionObject & err )
    {
    tube::ErrorMessage( "Reading volume: Exception caught: "
                        + std::string(err.GetDescription()) );
    timeCollector.Report();
    return EXIT_FAILURE;
    }

  typename TubeNetReaderType::Pointer vesselReader = TubeNetReaderType::New();
  vesselReader->SetFileName( inputVessel );
  try
    {
    vesselReader->Update();
    }
  catch( itk::ExceptionObject & err )
    {
    tube::ErrorMessage( "Reading vessel: Exception caught: "
                        + std::string(err.GetDescription()) );
    timeCollector.Report();
    return EXIT_FAILURE;
    }
  timeCollector.Stop("Load data");
  double progress = 0.1;
  progressReporter.Report( progress );

  timeCollector.Start("Sub-sample data");
  typedef itk::tube::SubSampleTubeTreeSpatialObjectFilter< TubeNetType, TubeType >
                                                         SubSampleTubeTreeFilterType;
  typename SubSampleTubeTreeFilterType::Pointer subSampleTubeTreeFilter =
    SubSampleTubeTreeFilterType::New();
  subSampleTubeTreeFilter->SetInput( vesselReader->GetGroup() );
  subSampleTubeTreeFilter->SetSampling( 100 );
#ifdef SlicerExecutionModel_USE_SERIALIZER
  if( !parametersToRestore.empty() )
    {
    // If the Json file has entries that describe the parameters for an
    // itk::tube::SubSampleTubeTreeSpatialObjectFilter, read them in, and set them on our
    // instance.
    if( parametersRoot.isMember( "SubSampleTubeTree" ) )
      {
      Json::Value & subSampleTubeTreeFilterValue = parametersRoot["SubSampleTubeTree"];
      typedef itk::tube::SubSampleTubeTreeSpatialObjectFilterSerializer<
        SubSampleTubeTreeFilterType > SerializerType;
      SerializerType::Pointer serializer = SerializerType::New();
      serializer->SetTargetObject( subSampleTubeTreeFilter );
      itk::JsonCppArchiver::Pointer archiver =
        dynamic_cast< itk::JsonCppArchiver * >( serializer->GetArchiver() );
      archiver->SetJsonValue( &subSampleTubeTreeFilterValue );
      serializer->DeSerialize();
      }
    }
#endif
  try
    {
    subSampleTubeTreeFilter->Update();
    }
  catch( itk::ExceptionObject & err )
    {
    tube::ErrorMessage( "Sub-sampling vessel: Exception caught: "
                        + std::string(err.GetDescription()) );
    timeCollector.Report();
    return EXIT_FAILURE;
    }

  timeCollector.Stop("Sub-sample data");
  progress = 0.2;
  progressReporter.Report( progress );


  typename ImageType::Pointer currentImage = reader->GetOutput();
  if( gaussianBlurStdDev > 0.0 )
    {
    timeCollector.Start("Gaussian Blur");

    typedef itk::RecursiveGaussianImageFilter< ImageType, ImageType >
      GaussianFilterType;
    typename GaussianFilterType::Pointer gaussianFilter;

    // Progress per iteration
    const double progressFraction = 0.1/Dimension;
    for( unsigned int ii = 0; ii < Dimension; ++ii )
      {
      gaussianFilter = GaussianFilterType::New();
      gaussianFilter->SetInput( currentImage );
      gaussianFilter->SetSigma( gaussianBlurStdDev );

      gaussianFilter->SetOrder(
               itk::RecursiveGaussianImageFilter<ImageType>::ZeroOrder );
      gaussianFilter->SetDirection( ii );
      tube::CLIFilterWatcher watcher( gaussianFilter,
                                      "Blur Filter 1D",
                                      CLPProcessInformation,
                                      progressFraction,
                                      progress,
                                      true );

      gaussianFilter->Update();
      currentImage = gaussianFilter->GetOutput();
      }

    timeCollector.Stop("Gaussian Blur");
    }

  timeCollector.Start("Compute Model Feature Weights");
  typedef itk::tube::Function::TubeExponentialResolutionWeightFunction<
    TubeType::TubePointType, FloatType >             WeightFunctionType;
  typedef RegistrationMethodType::FeatureWeightsType PointWeightsType;
  WeightFunctionType::Pointer weightFunction = WeightFunctionType::New();
  typedef itk::tube::TubePointWeightsCalculator< Dimension,
    TubeType, WeightFunctionType,
    PointWeightsType > PointWeightsCalculatorType;
  PointWeightsCalculatorType::Pointer resolutionWeightsCalculator
    = PointWeightsCalculatorType::New();
  resolutionWeightsCalculator->SetTubeTreeSpatialObject(
    subSampleTubeTreeFilter->GetOutput() );
  resolutionWeightsCalculator->SetPointWeightFunction( weightFunction );
  resolutionWeightsCalculator->Compute();
  PointWeightsType pointWeights = resolutionWeightsCalculator->GetPointWeights();

#ifdef SlicerExecutionModel_USE_SERIALIZER
  // Compute ultrasound probe geometry.
  if( !parametersToRestore.empty() )
    {
    // If the Json file has entries that describe the parameters for an
    // itk::tube::SubSampleTubeTreeSpatialObjectFilter, read them in, and set them on our
    // instance.
    if( parametersRoot.isMember( "UltrasoundProbeGeometryCalculator" ) )
      {
      timeCollector.Start("Compute probe geometry");
      Json::Value & probeGeometryCalculatorValue
        = parametersRoot["UltrasoundProbeGeometryCalculator"];

      typedef itk::tube::UltrasoundProbeGeometryCalculator< ImageType >
        GeometryCalculatorType;
      GeometryCalculatorType::Pointer geometryCalculator
        = GeometryCalculatorType::New();
      geometryCalculator->SetInput( reader->GetOutput() );

      typedef itk::tube::UltrasoundProbeGeometryCalculatorSerializer<
        GeometryCalculatorType > SerializerType;
      SerializerType::Pointer serializer = SerializerType::New();
      serializer->SetTargetObject( geometryCalculator );
      itk::JsonCppArchiver::Pointer archiver =
        dynamic_cast< itk::JsonCppArchiver * >( serializer->GetArchiver() );
      archiver->SetJsonValue( &probeGeometryCalculatorValue );
      serializer->DeSerialize();

      try
        {
        geometryCalculator->Update();
        }
      catch( itk::ExceptionObject & err )
        {
        tube::ErrorMessage( "Computing probe geometry: Exception caught: "
                            + std::string(err.GetDescription()) );
        timeCollector.Report();
        return EXIT_FAILURE;
        }

      if( parametersRoot.isMember( "UltrasoundProbeGeometryFile" ) )
        {
        const char * outputFile
          = parametersRoot["UltrasoundProbeGeometryFile"].asCString();
        std::ofstream geometryOutput( outputFile );
        if( !geometryOutput.is_open() )
          {
          tube::ErrorMessage( "Could not open geometry output file: "
                              + std::string( outputFile ) );
          timeCollector.Report();
          return EXIT_FAILURE;
          }

        const GeometryCalculatorType::OriginType ultrasoundProbeOrigin =
          geometryCalculator->GetUltrasoundProbeOrigin();
        geometryOutput << "UltrasoundProbeOrigin:";
        for( unsigned int ii = 0; ii < Dimension; ++ii )
          {
          geometryOutput << " " << ultrasoundProbeOrigin[ii];
          }
        geometryOutput << std::endl;

        const GeometryCalculatorType::RadiusType startOfAcquisitionRadius =
          geometryCalculator->GetStartOfAcquisitionRadius();
        geometryOutput << "GetStartOfAcquisitionRadius: "
                       << startOfAcquisitionRadius
                       << std::endl;
        }

      if( parametersRoot.isMember( "AngleOfIncidenceWeightFunction" ) )
        {
        Json::Value & angleOfIncidenceWeightFunctionValue =
          parametersRoot["AngleOfIncidenceWeightFunction"];

        typedef itk::tube::Function::TubeAngleOfIncidenceWeightFunction<
          TubeType::TubePointType, FloatType > AngleOfIncidenceWeightFunctionType;
        AngleOfIncidenceWeightFunctionType::Pointer angleOfIncidenceWeightFunction =
          AngleOfIncidenceWeightFunctionType::New();

        typedef itk::tube::TubeAngleOfIncidenceWeightFunctionSerializer<
          AngleOfIncidenceWeightFunctionType >
            AngleOfIncidenceSerializerType;
        AngleOfIncidenceSerializerType::Pointer angleOfIncidenceSerializer =
          AngleOfIncidenceSerializerType::New();
        angleOfIncidenceSerializer->SetTargetObject( angleOfIncidenceWeightFunction );
        itk::JsonCppArchiver::Pointer angleOfIncidenceArchiver =
          dynamic_cast< itk::JsonCppArchiver * >(
            angleOfIncidenceSerializer->GetArchiver() );
        angleOfIncidenceArchiver->SetJsonValue( &angleOfIncidenceWeightFunctionValue );
        angleOfIncidenceSerializer->DeSerialize();

        angleOfIncidenceWeightFunction->SetUltrasoundProbeOrigin(
          geometryCalculator->GetUltrasoundProbeOrigin() );

        typedef itk::tube::TubePointWeightsCalculator< Dimension,
          TubeType, AngleOfIncidenceWeightFunctionType, PointWeightsType >
            AngleOfIncidenceWeightsCalculatorType;
        AngleOfIncidenceWeightsCalculatorType::Pointer angleOfIncidenceWeightsCalculator =
          AngleOfIncidenceWeightsCalculatorType::New();
        angleOfIncidenceWeightsCalculator->SetPointWeightFunction(
          angleOfIncidenceWeightFunction );
        angleOfIncidenceWeightsCalculator->SetTubeTreeSpatialObject(
          subSampleTubeTreeFilter->GetOutput() );
        angleOfIncidenceWeightsCalculator->Compute();
        const PointWeightsType & angleOfIncidenceWeights =
          angleOfIncidenceWeightsCalculator->GetPointWeights();
        for( itk::SizeValueType ii = 0; ii < pointWeights.GetSize(); ++ii )
          {
          pointWeights[ii] *= angleOfIncidenceWeights[ii];
          }
        }

      timeCollector.Stop("Compute probe geometry");
      }
    }

  if( parametersRoot.isMember( "TubePointWeightsFile" ) )
    {
    Json::Value weightsJSONRoot;
    weightsJSONRoot["TubePointWeights"] = Json::Value( Json::arrayValue );
    Json::Value & weightsJSON = weightsJSONRoot["TubePointWeights"];
    weightsJSON.resize( pointWeights.GetSize() );
    for( itk::SizeValueType ii = 0; ii < pointWeights.GetSize(); ++ii )
      {
      weightsJSON[static_cast<Json::ArrayIndex>(ii)] = pointWeights[ii];
      }

    Json::FastWriter writer;
    const std::string weightsString = writer.write( weightsJSONRoot );

    Json::Value & tubePointWeightsFileValue =
      parametersRoot["TubePointWeightsFile"];
    std::ofstream tubePointWeightsFile( tubePointWeightsFileValue.asCString() );
    if( !tubePointWeightsFile.is_open() )
      {
      tube::ErrorMessage( "Could not open tube point weights file: "
                          + tubePointWeightsFileValue.asString() );
      timeCollector.Report();
      return EXIT_FAILURE;
      }
    tubePointWeightsFile << weightsString;
    }
#endif
  timeCollector.Stop("Compute Model Feature Weights");


  timeCollector.Start("Register image to tube");

  typename RegistrationMethodType::Pointer registrationMethod =
    RegistrationMethodType::New();

  registrationMethod->SetFixedImage( currentImage );
  registrationMethod->SetMovingSpatialObject( subSampleTubeTreeFilter->GetOutput() );
  registrationMethod->SetFeatureWeights( pointWeights );

  // Set Optimizer parameters.
  typename RegistrationMethodType::OptimizerType::Pointer optimizer =
    registrationMethod->GetOptimizer();
  itk::GradientDescentOptimizer * gradientDescentOptimizer =
    dynamic_cast< itk::GradientDescentOptimizer * >( optimizer.GetPointer() );
  if( gradientDescentOptimizer )
    {
    gradientDescentOptimizer->SetLearningRate( 0.1 );
    gradientDescentOptimizer->SetNumberOfIterations( 1000 );
    }
#ifdef SlicerExecutionModel_USE_SERIALIZER
  if( !parametersToRestore.empty() )
    {
    // If the Json file has entries that describe the parameters for an
    // itk::GradientDescentOptimizer, read them in, and set them on our
    // gradientDescentOptimizer instance.
    if( parametersRoot.isMember( "GradientDescentOptimizer" ) )
      {
      Json::Value & gradientDescentOptimizerValue =
        parametersRoot["GradientDescentOptimizer"];
      typedef itk::GradientDescentOptimizerSerializer SerializerType;
      SerializerType::Pointer serializer = SerializerType::New();
      serializer->SetTargetObject( gradientDescentOptimizer );
      itk::JsonCppArchiver::Pointer archiver =
        dynamic_cast< itk::JsonCppArchiver * >( serializer->GetArchiver() );
      archiver->SetJsonValue( &gradientDescentOptimizerValue );
      serializer->DeSerialize();
      }
    }
#endif

  // TODO: This is hard-coded now, which is sufficient since
  // ImageToTubeRigidMetric only uses a Euler3DTransform.  Will need to adjust
  // to the transform parameters in the future at compile time.
  const unsigned int NumberOfParameters = 6;
  typedef itk::tube::RecordOptimizationParameterProgressionCommand< NumberOfParameters >
    RecordParameterProgressionCommandType;
  RecordParameterProgressionCommandType::Pointer
    recordParameterProgressionCommand = RecordParameterProgressionCommandType::New();
  if( !parameterProgression.empty() )
    {
    // Record the optimization parameter progression and write to a file.
    recordParameterProgressionCommand->SetFileName( parameterProgression );
    optimizer->AddObserver( itk::StartEvent(), recordParameterProgressionCommand );
    optimizer->AddObserver( itk::IterationEvent(), recordParameterProgressionCommand );
    }

  try
    {
    registrationMethod->Initialize();
    registrationMethod->Update();
    }
  catch( itk::ExceptionObject & err )
    {
    tube::ErrorMessage( "Performing registration: Exception caught: "
                        + std::string(err.GetDescription()) );
    timeCollector.Report();
    return EXIT_FAILURE;
    }
  progress = 0.9;
  progressReporter.Report( progress );

  TransformType* registrationTransform =
    dynamic_cast<TransformType *>(registrationMethod->GetTransform());
  registrationTransform->SetParameters(
    registrationMethod->GetLastTransformParameters() );
  std::ostringstream parametersMessage;
  parametersMessage << "Transform Parameters: "
                    << registrationMethod->GetLastTransformParameters();
  tube::InformationMessage( parametersMessage.str() );
  parametersMessage.str( "" );
  parametersMessage << "Transform Center Of Rotation: "
                    << registrationTransform->GetFixedParameters();
  tube::InformationMessage( parametersMessage.str() );
  timeCollector.Stop("Register image to tube");


  timeCollector.Start("Save data");

  if( !parameterProgression.empty() )
    {
    recordParameterProgressionCommand->SetFixedParameters(
      registrationTransform->GetFixedParameters() );
    recordParameterProgressionCommand->WriteParameterProgressionToFile();
    }

  itk::TransformFileWriter::Pointer writer = itk::TransformFileWriter::New();
  writer->SetFileName( outputTransform.c_str() );
  writer->SetInput( registrationTransform );
  try
    {
    writer->Update();
    }
  catch( itk::ExceptionObject & err )
    {
    tube::ErrorMessage( "Writing transform: Exception caught: "
      + std::string(err.GetDescription()) );
    timeCollector.Report();
    return EXIT_FAILURE;
    }
  timeCollector.Stop("Save data");
  progress = 1.0;
  progressReporter.Report( progress );
  progressReporter.End();

  timeCollector.Report();
  return EXIT_SUCCESS;
}

// Main
int main( int argc, char * argv[] )
{
  PARSE_ARGS;

  // You may need to update this line if, in the project's .xml CLI file,
  //   you change the variable name for the inputVolume.
  return tube::ParseArgsAndCallDoIt( inputVolume, argc, argv );
}
