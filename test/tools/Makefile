CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -I.
LDFLAGS = -ljson-c -lgtest -lgtest_main -pthread

# Source files
FRAMEWORK_SOURCES = layout_test_framework.cpp
TEST_SOURCES = test_layout_auto.cpp
OBJECTS = $(FRAMEWORK_SOURCES:.cpp=.o) $(TEST_SOURCES:.cpp=.o)

# Target
TARGET = test_layout_auto

# Default target
all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

test: $(TARGET)
	./$(TARGET) --gtest_output=xml:../reports/test_results.xml

.PHONY: all clean test
