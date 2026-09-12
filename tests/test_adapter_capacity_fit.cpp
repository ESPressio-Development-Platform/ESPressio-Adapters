#include <ESPressio_AdapterCapacityValidation.hpp>
#include <array>
using namespace ESPressio::Adapters;
constexpr std::array<AdapterByteClassShape,3> classes{{{16,2},{64,1},{128,1}}};
constexpr std::array<ProtectedCapacityRequirement,3> requirements{{
    {AdapterDirection::Outbound,AdapterServiceClass::Critical,1,100},
    {AdapterDirection::Outbound,AdapterServiceClass::Critical,1,60},
    {AdapterDirection::Outbound,AdapterServiceClass::Critical,2,12}}};
static_assert(ValidateCapacityFit(classes,4,requirements).Status==CapacityFitStatus::Success);
constexpr std::array<AdapterByteClassShape,2> tooSmall{{{16,3},{64,1}}};
constexpr std::array<ProtectedCapacityRequirement,2> large{{
    {AdapterDirection::Outbound,AdapterServiceClass::Critical,2,50},
    {AdapterDirection::Outbound,AdapterServiceClass::Critical,1,12}}};
static_assert(ValidateCapacityFit(tooSmall,3,large).Status==CapacityFitStatus::ByteSlotShortage);
static_assert(ValidateCapacityFit(classes,3,requirements).Status==CapacityFitStatus::RecordShortage);
int main(){}
