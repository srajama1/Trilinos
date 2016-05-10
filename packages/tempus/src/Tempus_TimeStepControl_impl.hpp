#ifndef TEMPUS_TIMESTEPCONTROL_IMPL_HPP
#define TEMPUS_TIMESTEPCONTROL_IMPL_HPP

// Teuchos
#include "Teuchos_ScalarTraits.hpp"
#include "Teuchos_StandardParameterEntryValidators.hpp"
#include "Teuchos_VerboseObjectParameterListHelpers.hpp"

using Teuchos::RCP;
using Teuchos::rcp;
using Teuchos::ParameterList;

namespace {

  static std::string timeMin_name    = "Minimum Simulation Time";
  static double      timeMin_default = 0.0;

  static std::string timeMax_name    = "Maximum Simulation Time";
  static double      timeMax_default = std::numeric_limits<double>::max();

  static std::string dtMin_name    = "Minimum Time Step";
  static double      dtMin_default = std::numeric_limits<double>::epsilon();

  static std::string dtMax_name    = "Maximum Time Step";
  static double      dtMax_default = std::numeric_limits<double>::max();

  static std::string iStepMin_name    = "Minimum Time Step Index";
  static int         iStepMin_default = 0;

  static std::string iStepMax_name    = "Maximum Time Step Index";
  static int         iStepMax_default = std::numeric_limits<int>::max();

  static std::string errorMaxAbs_name    = "Maximum Absolute Error";
  static double      errorMaxAbs_default = 1.0e-08;

  static std::string errorMaxRel_name    = "Maximum Relative Error";
  static double      errorMaxRel_default = 1.0e-08;

  static std::string orderMin_name    = "Minimum Time Integration Order";
  static int         orderMin_default = 1;

  static std::string orderMax_name    = "Maximum Time Integration Order";
  static int         orderMax_default = 4;

  static std::string Constant_name    = "Constant";
  static std::string Variable_name    = "Variable";
  static std::string StepType_name    = "Step Type";
  static std::string StepType_default = Variable_name;

  Teuchos::Array<std::string> StepType_names = Teuchos::tuple<std::string>(
      Constant_name,
      Variable_name);

  const RCP<Teuchos::StringToIntegralParameterEntryValidator<Tempus::StepType> >
    StepTypeValidator = Teuchos::rcp(
        new Teuchos::StringToIntegralParameterEntryValidator<Tempus::StepType>(
          StepType_names,
          Teuchos::tuple<Tempus::StepType>(
            Tempus::CONSTANT_STEP_SIZE,
            Tempus::VARIABLE_STEP_SIZE),
          StepType_name));

  static std::string outputTimeList_name         = "Output Time List";
  static std::string outputTimeList_default      = "";
  static std::string outputIndexList_name        = "Output Index List";
  static std::string outputIndexList_default     = "";
  static std::string outputTimeInterval_name     = "Output Time Interval";
  static double      outputTimeInterval_default  = 100.0;
  static std::string outputIndexInterval_name    = "Output Index Interval";
  static int         outputIndexInterval_default = 100;

  static std::string nFailuresMax_name    =
    "Maximum Number of Stepper Failures";
  static int         nFailuresMax_default = 10.0;
  static std::string nConsecutiveFailuresMax_name    =
    "Maximum Number of Consecutive Stepper Failures";
  static int         nConsecutiveFailuresMax_default = 5;

} // namespace


namespace Tempus {

// TimeStepControl definitions:
template<class Scalar>
TimeStepControl<Scalar>::TimeStepControl()
{
  pList->validateParametersAndSetDefaults(*this->getValidParameters());
  this->setParameterList(pList);
}

template<class Scalar>
TimeStepControl<Scalar>::TimeStepControl(
  RCP<ParameterList> pList_, const Scalar dtConstant_)
  : dtConstant(dtConstant_)
{
  if (pList_ == Teuchos::null)
    pList->validateParametersAndSetDefaults(*this->getValidParameters());
  else
    pList = pList_;

  this->setParameterList(pList);

  TEUCHOS_TEST_FOR_EXCEPTION(
    (dtConstant < Teuchos::ScalarTraits<Scalar>::zero() ), std::logic_error,
    "Error - Negative constant time step.  dtConstant = "<<dtConstant<<")\n");
  TEUCHOS_TEST_FOR_EXCEPTION(
    (dtConstant < dtMin || dtConstant > dtMax ), std::out_of_range,
    "Error - Constant time step is out of range.\n"
    << "    [dtMin, dtMax] = [" << dtMin << ", " << dtMax << "]\n"
    << "    dtConstant = " << dtConstant << "\n");

}

template<class Scalar>
TimeStepControl<Scalar>::TimeStepControl(const TimeStepControl<Scalar>& tsc_)
  : timeMin      (tsc_.timeMin    ),
    timeMax      (tsc_.timeMax    ),
    dtMin        (tsc_.dtMin      ),
    dtMax        (tsc_.dtMax      ),
    iStepMin     (tsc_.iStepMin   ),
    iStepMax     (tsc_.iStepMax   ),
    errorMaxAbs  (tsc_.errorMaxAbs),
    errorMaxRel  (tsc_.errorMaxRel),
    orderMin     (tsc_.orderMin   ),
    orderMax     (tsc_.orderMax   ),
    stepType     (tsc_.stepType   ),
    dtConstant   (tsc_.dtConstant ),
    outputIndices(tsc_.outputIndices),
    outputTimes  (tsc_.outputTimes),
    nFailuresMax           (tsc_.nFailuresMax),
    nConsecutiveFailuresMax(tsc_.nConsecutiveFailuresMax),
    pList        (tsc_.pList  )
{}


template<class Scalar>
void TimeStepControl<Scalar>::getNextTimeStep(
  const RCP<SolutionHistory<Scalar> > & solutionHistory,
  const bool stepperStatus, bool & integratorStatus) const
{
  RCP<SolutionState<Scalar> > workingState = solutionHistory->getWorkingState();
  RCP<SolutionStateMetaData<Scalar> > metaData = workingState->metaData;
  const Scalar time = metaData->time;
  const int iStep = metaData->iStep;
  const Scalar errorAbs = metaData->errorAbs;
  const Scalar errorRel = metaData->errorRel;
  int & order = metaData->order;
  Scalar & dt = metaData->dt;
  Scalar & suggestedDt = metaData->suggestedDt;
  bool & output = metaData->output;

  output = false;

  if (dt < dtMin) {
    RCP<Teuchos::FancyOStream> out = this->getOStream();
    Teuchos::OSTab ostab(out,1,"getNextTimeStep");
    *out << "Warning - Time step size (=" << dt << ") is less than\n"
         << "  minimum time step size (=" << dtMin << ")\n."
         << "  Resetting to minimum time step size." << std::endl;
    dt = dtMin;
  }

  if (stepType == CONSTANT_STEP_SIZE) {

    // Stepper failure
    if (stepperStatus != true) {
      if (order+1 <= orderMax) {
        order++;
        RCP<Teuchos::FancyOStream> out = this->getOStream();
        Teuchos::OSTab ostab(out,1,"getNextTimeStep");
        *out << "Warning - Stepper failure with constant time step.\n"
             << "  Try increasing order.  order = " << order << std::endl;
      } else {
        RCP<Teuchos::FancyOStream> out = this->getOStream();
        Teuchos::OSTab ostab(out,1,"getNextTimeStep");
        *out << "Failure - Stepper failed and can not change time step size "
             << "or order!\n"
             << "    Time step type == CONSTANT_STEP_SIZE\n"
             << "    order = " << order << std::endl;
        integratorStatus = false;
        return;
      }
    }

    // Absolute error failure
    if (errorAbs > errorMaxAbs) {
      if (order+1 <= orderMax) {
        order++;
        RCP<Teuchos::FancyOStream> out = this->getOStream();
        Teuchos::OSTab ostab(out,1,"getNextTimeStep");
        *out << "Warning - Absolute error is too large with constant time step.\n"
             << "  (errorAbs ="<<errorAbs<<") > (errorMaxAbs ="<<errorMaxAbs<<")"
             << "  Try increasing order.  order = " << order << std::endl;
      } else {
        RCP<Teuchos::FancyOStream> out = this->getOStream();
        Teuchos::OSTab ostab(out,1,"getNextTimeStep");
        *out << "Failure - Absolute error failed and can not change time step "
             << "size or order!\n"
             << "  Time step type == CONSTANT_STEP_SIZE\n"
             << "  order = " << order
             << "  (errorAbs ="<<errorAbs<<") > (errorMaxAbs ="<<errorMaxAbs<<")"
             << std::endl;
        integratorStatus = false;
        return;
      }
    }

    // Relative error failure
    if (errorRel > errorMaxRel) {
      if (order+1 <= orderMax) {
        order++;
        RCP<Teuchos::FancyOStream> out = this->getOStream();
        Teuchos::OSTab ostab(out,1,"getNextTimeStep");
        *out << "Warning - Relative error is too large with constant time step.\n"
             << "  (errorRel ="<<errorRel<<") > (errorMaxRel ="<<errorMaxRel<<")"
             << "  Try increasing order.  order = " << order << std::endl;
      } else {
        RCP<Teuchos::FancyOStream> out = this->getOStream();
        Teuchos::OSTab ostab(out,1,"getNextTimeStep");
        *out << "Failure - Relative error failed and can not change time step "
             << "size or order!\n"
             << "  Time step type == CONSTANT_STEP_SIZE\n"
             << "  order = " << order
             << "  (errorRel ="<<errorRel<<") > (errorMaxRel ="<<errorMaxRel<<")"
             << std::endl;
        integratorStatus = false;
        return;
      }
    }

    // Check if to output this step
    std::vector<int>::const_iterator it;
    it = std::find(outputIndices.begin(), outputIndices.end(), iStep+1);
    if (it != outputIndices.end()) output = true;

    if (!output) {
      for (int i=0; i < outputTimes.size(); ++i) {
        if (time < outputTimes[i] && outputTimes[i] <= time + dt) {
          output = true;
          break;
        }
      }
    }

    if (time + dt < timeMin || time + dt > timeMax) {
      RCP<Teuchos::FancyOStream> out = this->getOStream();
      Teuchos::OSTab ostab(out,1,"getNextTimeStep");
      *out << "Warning - Time step moves time outside desired time range.\n"
           << "  [timeMin, timeMax] = [" << timeMin << ", " << timeMax << "]\n"
           << "  T + dt = "<< time <<" + "<< dt<<" = "<< time + dt <<"\n";
      output = true;
    }

    // Consistency checks
    TEUCHOS_TEST_FOR_EXCEPTION(
      (dt != dtConstant), std::out_of_range,
      "Error - ( dt = "<< dt <<") != ( dtConstant = "<< dtConstant <<" )!\n");

    TEUCHOS_TEST_FOR_EXCEPTION(
      (order < orderMin || order > orderMax), std::out_of_range,
      "Error - Solution order is out of range and can not change "
      "time step size!\n"
      "    Time step type == CONSTANT_STEP_SIZE\n"
      "    [order_min, order_max] = [" << orderMin << ", " << orderMax << "]\n"
      "    order = " << order << "\n");

  } else { // VARIABLE_STEP_SIZE

    // \todo The following controls should be generalized to plugable options.
    if (stepperStatus != true) dt /=2;
    if (errorAbs > errorMaxAbs) dt /= 2;
    if (errorRel > errorMaxRel) dt /= 2;
    if (order < orderMin) dt *= 2;
    if (order > orderMax) dt /= 2;

    if (dt < dtMin) dt = dtMin;
    if (dt > dtMax) dt = dtMax;

    if (time + dt > timeMax ) dt = timeMax - time;

    // Check if to output this step
    std::vector<int>::const_iterator it =
      std::find(outputIndices.begin(), outputIndices.end(), iStep);
    if (it != outputIndices.end()) output = true;

    if (!output) {
      for (int i=0; i < outputTimes.size(); ++i) {
        if (time < outputTimes[i] && outputTimes[i] <= time + dt) {
          output = true;
          dt = outputTimes[i] - time;
          break;
        }
      }
    }

    // Consistency checks
    TEUCHOS_TEST_FOR_EXCEPTION(
      (time + dt < timeMin), std::out_of_range,
      "Error - Time step does not move time INTO time range.\n"
      "    [timeMin, timeMax] = [" << timeMin << ", " << timeMax << "]\n"
      "    T + dt = " << time <<" + "<< dt <<" = " << time + dt << "\n");

    TEUCHOS_TEST_FOR_EXCEPTION(
      (time + dt > timeMax), std::out_of_range,
      "Error - Time step move time OUT OF time range.\n"
      "    [timeMin, timeMax] = [" << timeMin << ", " << timeMax << "]\n"
      "    T + dt = " << time <<" + "<< dt <<" = " << time + dt << "\n");
  }

  suggestedDt = dt;
  return;
}


/// Test if time is within range: include timeMin and exclude timeMax.
template<class Scalar>
bool TimeStepControl<Scalar>::timeInRange(const Scalar time) const{
  const Scalar relTol = 1.0e-14;
  bool tir = (timeMin*(1.0-relTol) <= time and time < timeMax*(1.0-relTol));
  return tir;
}


template<class Scalar>
bool TimeStepControl<Scalar>::indexInRange(const int iStep) const{
  bool iir = (iStepMin <= iStep and iStep < iStepMax);
  return iir;
}


template<class Scalar>
std::string TimeStepControl<Scalar>::description() const
{
  std::string name = "Tempus::TimeStepControl";
  return(name);
}


template<class Scalar>
void TimeStepControl<Scalar>::describe(
   Teuchos::FancyOStream               &out,
   const Teuchos::EVerbosityLevel      verbLevel) const
{
  if (verbLevel == Teuchos::VERB_EXTREME) {
    out << description() << "::describe:" << std::endl
        << "timeMin      = " << timeMin      << std::endl
        << "timeMax      = " << timeMax      << std::endl
        << "dtMin        = " << dtMin        << std::endl
        << "dtMax        = " << dtMax        << std::endl
        << "iStepMin     = " << iStepMin     << std::endl
        << "iStepMax     = " << iStepMax     << std::endl
        << "errorMaxAbs  = " << errorMaxAbs  << std::endl
        << "errorMaxRel  = " << errorMaxRel  << std::endl
        << "orderMin     = " << orderMin     << std::endl
        << "orderMax     = " << orderMax     << std::endl
        << "stepType     = " << stepType     << std::endl
        << "nFailuresMax = " << nFailuresMax << std::endl
        << "nConsecutiveFailuresMax = " << nConsecutiveFailuresMax << std::endl
        << "pList        = " << pList        << std::endl;
  }
}


template <class Scalar>
void TimeStepControl<Scalar>::setParameterList(
  RCP<ParameterList> const& pList_)
{
  TEUCHOS_TEST_FOR_EXCEPT(is_null(pList_));
  pList_->validateParameters(*this->getValidParameters());
  pList = pList_;

  Teuchos::readVerboseObjectSublist(&*pList,this);

  timeMin     = pList->get<double>(timeMin_name    , timeMin_default    );
  timeMax     = pList->get<double>(timeMax_name    , timeMax_default    );
  TEUCHOS_TEST_FOR_EXCEPTION(
    (timeMin > timeMax ), std::logic_error,
    "Error - Inconsistent time range.\n"
    "    (timeMin = "<<timeMin<<") > (timeMax = "<<timeMax<<")\n");

  dtMin       = pList->get<double>(dtMin_name      , dtMin_default      );
  dtMax       = pList->get<double>(dtMax_name      , dtMax_default      );
  TEUCHOS_TEST_FOR_EXCEPTION(
    (dtMin < Teuchos::ScalarTraits<Scalar>::zero() ), std::logic_error,
    "Error - Negative minimum time step.  dtMin = "<<dtMin<<")\n");
  TEUCHOS_TEST_FOR_EXCEPTION(
    (dtMax < Teuchos::ScalarTraits<Scalar>::zero() ), std::logic_error,
    "Error - Negative maximum time step.  dtMax = "<<dtMax<<")\n");
  TEUCHOS_TEST_FOR_EXCEPTION(
    (dtMin > dtMax ), std::logic_error,
    "Error - Inconsistent time step range.\n"
    "    (dtMin = "<<dtMin<<") > (dtMax = "<<dtMax<<")\n");

  iStepMin    = pList->get<int>   (iStepMin_name   , iStepMin_default   );
  iStepMax    = pList->get<int>   (iStepMax_name   , iStepMax_default   );
  TEUCHOS_TEST_FOR_EXCEPTION(
    (iStepMin > iStepMax ), std::logic_error,
    "Error - Inconsistent time index range.\n"
    "    (iStepMin = "<<iStepMin<<") > (iStepMax = "<<iStepMax<<")\n");

  errorMaxAbs = pList->get<double>(errorMaxAbs_name, errorMaxAbs_default);
  errorMaxRel = pList->get<double>(errorMaxRel_name, errorMaxRel_default);
  TEUCHOS_TEST_FOR_EXCEPTION(
    (errorMaxAbs < Teuchos::ScalarTraits<Scalar>::zero() ), std::logic_error,
    "Error - Negative maximum time step.  errorMaxAbs = "<<errorMaxAbs<<")\n");
  TEUCHOS_TEST_FOR_EXCEPTION(
    (errorMaxRel < Teuchos::ScalarTraits<Scalar>::zero() ), std::logic_error,
    "Error - Negative maximum time step.  errorMaxRel = "<<errorMaxRel<<")\n");

  orderMin    = pList->get<int>   (orderMin_name   , orderMin_default   );
  orderMax    = pList->get<int>   (orderMax_name   , orderMax_default   );
  TEUCHOS_TEST_FOR_EXCEPTION(
    (orderMin < Teuchos::ScalarTraits<Scalar>::zero() ), std::logic_error,
    "Error - Negative minimum order.  orderMin = "<<orderMin<<")\n");
  TEUCHOS_TEST_FOR_EXCEPTION(
    (orderMax < Teuchos::ScalarTraits<Scalar>::zero() ), std::logic_error,
    "Error - Negative maximum order.  orderMax = "<<orderMax<<")\n");
  TEUCHOS_TEST_FOR_EXCEPTION(
    (orderMin > orderMax ), std::logic_error,
    "Error - Inconsistent order range.\n"
    "    (orderMin = "<<orderMin<<") > (orderMax = "<<orderMax<<")\n");

  stepType = StepTypeValidator->getIntegralValue(
      *pList, StepType_name, StepType_default);


  // Parse output times
  {
    outputTimes.clear();
    std::string str =
      pList->get<std::string>(outputTimeList_name, outputTimeList_default);
    std::string delimiters(",");
    std::string::size_type lastPos = str.find_first_not_of(delimiters, 0);
    std::string::size_type pos     = str.find_first_of(delimiters, lastPos);
    while ((pos != std::string::npos) || (lastPos != std::string::npos)) {
      std::string token = str.substr(lastPos,pos-lastPos);
      outputTimes.push_back(Scalar(std::stod(token)));
      if(pos==std::string::npos)
        break;

      lastPos = str.find_first_not_of(delimiters, pos);
      pos = str.find_first_of(delimiters, lastPos);
    }

    Scalar outputTimeInterval =
     pList->get<double>(outputTimeInterval_name, outputTimeInterval_default);
    Scalar output_t = timeMin;
    while (output_t <= timeMax) {
      outputTimes.push_back(output_t);
      output_t += outputTimeInterval;
    }

    // order output times
    std::sort(outputTimes.begin(),outputTimes.end());
  }

  // Parse output indices
  {
    outputIndices.clear();
    std::string str =
      pList->get<std::string>(outputIndexList_name, outputIndexList_default);
    std::string delimiters(",");
    std::string::size_type lastPos = str.find_first_not_of(delimiters, 0);
    std::string::size_type pos     = str.find_first_of(delimiters, lastPos);
    while ((pos != std::string::npos) || (lastPos != std::string::npos)) {
      std::string token = str.substr(lastPos,pos-lastPos);
      outputIndices.push_back(int(std::stoi(token)));
      if(pos==std::string::npos)
        break;

      lastPos = str.find_first_not_of(delimiters, pos);
      pos = str.find_first_of(delimiters, lastPos);
    }

    Scalar outputIndexInterval =
      pList->get<int>(outputIndexInterval_name, outputIndexInterval_default);
    Scalar output_i = iStepMin;
    while (output_i <= iStepMax) {
      outputIndices.push_back(output_i);
      output_i += outputIndexInterval;
    }

    // order output indices
    std::sort(outputIndices.begin(),outputIndices.end());
  }

  nFailuresMax = pList->get<int>(nFailuresMax_name, nFailuresMax_default);
  nConsecutiveFailuresMax = pList->get<int>(
    nConsecutiveFailuresMax_name, nConsecutiveFailuresMax_default);
  return;
}


template<class Scalar>
RCP<const ParameterList> TimeStepControl<Scalar>::getValidParameters() const
{
  static RCP<ParameterList> validPL;

  if (is_null(validPL)) {

    RCP<ParameterList> pl = Teuchos::parameterList();
    Teuchos::setupVerboseObjectSublist(&*pl);

    pl->set(timeMin_name    , timeMin_default    , "Minimum simulation time");
    pl->set(timeMax_name    , timeMax_default    , "Maximum simulation time");
    pl->set(dtMin_name      , dtMin_default      , "Minimum time step size");
    pl->set(dtMax_name      , dtMax_default      , "Maximum time step size");
    pl->set(iStepMin_name   , iStepMin_default   , "Minimum time step index");
    pl->set(iStepMax_name   , iStepMax_default   , "Maximum time step index");
    pl->set(errorMaxAbs_name, errorMaxAbs_default, "Maximum absolute error");
    pl->set(errorMaxRel_name, errorMaxRel_default, "Maximum relative error");
    pl->set(orderMin_name, orderMin_default, "Minimum time integration order");
    pl->set(orderMax_name, orderMax_default, "Maximum time integration order");

    pl->set(StepType_name, StepType_default,
      "Step Type indicates whether the Integrator will allow the time step "
      "to be modified the Stepper.\n"
      "  'Constant' - Integrator will take constant time step sizes.\n"
      "  'Variable' - Integrator will allow changes to the time step size.\n",
//      "'Unmodifiable' - Integrator will not allow the Stepper to take a "
//                     "time step different than one requested.\n"
//      "'Modifiable' - Integrator will use a time step size from the Stepper "
//                     "that is different than the one requested.\n"
      StepTypeValidator);

    pl->set(outputTimeList_name, outputTimeList_default,
      "Comma deliminated list of output times");
    pl->set(outputIndexList_name, outputIndexList_default,
      "Comma deliminated list of output indices");
    pl->set(outputTimeInterval_name, outputTimeInterval_default,
      "Output time interval (e.g., every 100.0 integrated time");
    pl->set(outputIndexInterval_name, outputIndexInterval_default,
      "Output index interval (e.g., every 100 time steps");

    pl->set(nFailuresMax_name, nFailuresMax_default,
      "Maximum number of Stepper failures");
    pl->set(nConsecutiveFailuresMax_name, nConsecutiveFailuresMax_default,
      "Maximum number of consecutive Stepper failures");

    validPL = pl;

  }
  return validPL;
}


template <class Scalar>
RCP<ParameterList>
TimeStepControl<Scalar>::getNonconstParameterList()
{
  return(pList);
}


template <class Scalar>
RCP<ParameterList> TimeStepControl<Scalar>::unsetParameterList()
{
  RCP<ParameterList> temp_plist = pList;
  pList = Teuchos::null;
  return(temp_plist);
}


} // namespace Tempus
#endif // TEMPUS_TIMESTEPCONTROL_IMPL_HPP
