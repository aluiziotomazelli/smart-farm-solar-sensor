// host_test/mocks/mock_command_handler.hpp
#pragma once

#include "gmock/gmock.h"
#include "interfaces/i_command_handler.hpp"

class MockCommandHandler : public ICommandHandler
{
public:
    MOCK_METHOD(CommandProcessResult, process, (), (override));
};
