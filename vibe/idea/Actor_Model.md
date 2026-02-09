# Actor Model in Programming Languages

## Overview

The Actor Model is a mathematical model of concurrent computation that treats "actors" as the universal primitives of concurrent computation. Actors communicate exclusively through message passing and maintain their own private state.

## Core Principles

1. **Encapsulation**: Each actor has private state that cannot be accessed directly
2. **Message Passing**: Actors communicate only through asynchronous messages
3. **Location Transparency**: Actors can be local or distributed across networks
4. **Fault Tolerance**: Actor failures are isolated and don't affect other actors
5. **Concurrency**: Actors process messages concurrently without shared state

## Language Implementations

### 1. Erlang - The Original Actor Language

Erlang was designed from the ground up with the actor model as its core concurrency primitive.

```erlang
-module(data_processor).
-behaviour(gen_server).

%% API
-export([start_link/0, process/1, get_stats/0]).

%% gen_server callbacks
-export([init/1, handle_call/3, handle_cast/2]).

-record(state, {processed = 0}).

start_link() ->
    gen_server:start_link({local, ?MODULE}, ?MODULE, [], []).

process(Data) ->
    gen_server:cast(?MODULE, {process, Data}).

get_stats() ->
    gen_server:call(?MODULE, get_stats).

init([]) ->
    {ok, #state{}}.

handle_call(get_stats, _From, State) ->
    {reply, {ok, State#state.processed}, State}.

handle_cast({process, Data}, State) ->
    Result = process_data(Data),
    io:format("Processed: ~p~n", [Result]),
    {noreply, State#state{processed = State#state.processed + 1}}.

process_data(Data) ->
    % Simulate data processing
    string:uppercase(Data).
```

### 2. Elixir - Modern Erlang

Elixir provides a more modern syntax while maintaining Erlang's actor foundation.

```elixir
defmodule DataProcessor do
  use GenServer

  # Client API
  def start_link(initial_state \\ %{processed: 0}) do
    GenServer.start_link(__MODULE__, initial_state, name: __MODULE__)
  end

  def process(data) do
    GenServer.cast(__MODULE__, {:process, data})
  end

  def get_stats do
    GenServer.call(__MODULE__, :get_stats)
  end

  # Server Callbacks
  def init(state) do
    {:ok, state}
  end

  def handle_call(:get_stats, _from, state) do
    {:reply, {:ok, state.processed}, state}
  end

  def handle_cast({:process, data}, state) do
    result = process_data(data)
    IO.puts("Processed: #{result}")
    new_state = %{state | processed: state.processed + 1}
    {:noreply, new_state}
  end

  defp process_data(data) do
    String.upcase(data)
  end
end

# Usage
{:ok, _pid} = DataProcessor.start_link()
DataProcessor.process("hello world")
{:ok, count} = DataProcessor.get_stats()
```

### 3. Pony - Type-Safe Actors

Pony is designed with actors as first-class citizens and compile-time memory safety.

```pony
actor DataProcessor
  var _processed: U64 = 0
  let _env: Env

  new create(env: Env) =>
    _env = env

  be process(data: String) =>
    let result = process_data(data)
    _env.out.print("Processed: " + result)
    _processed = _processed + 1

  be get_stats(callback: {(U64)} val) =>
    callback(_processed)

  fun process_data(data: String): String =>
    data.upper()

// Usage
actor Main
  new create(env: Env) =>
    let processor = DataProcessor(env)
    processor.process("hello world")
    processor.get_stats({(count: U64) => env.out.print("Count: " + count.string())})
```

### 4. Scala with Akka

Akka provides a mature actor implementation for the JVM ecosystem.

```scala
import akka.actor.{Actor, ActorRef, ActorSystem, Props}

case class ProcessData(data: String)
case object GetStats
case class StatsResponse(processed: Int)

class DataProcessor extends Actor {
  var processed = 0

  def receive = {
    case ProcessData(data) =>
      val result = processData(data)
      println(s"Processed: $result")
      processed += 1

    case GetStats =>
      sender() ! StatsResponse(processed)
  }

  private def processData(data: String): String = {
    data.toUpperCase
  }
}

// Usage
object ActorExample extends App {
  val system = ActorSystem("DataProcessingSystem")
  val processor = system.actorOf(Props[DataProcessor], "processor")

  processor ! ProcessData("hello world")
  processor ! GetStats

  system.terminate()
}
```

### 5. Rust with Actix

Actix provides high-performance actors for Rust.

```rust
use actix::prelude::*;

#[derive(Message)]
#[rtype(result = "()")]
struct ProcessData {
    data: String,
}

#[derive(Message)]
#[rtype(result = "u64")]
struct GetStats;

struct DataProcessor {
    processed: u64,
}

impl DataProcessor {
    fn new() -> Self {
        DataProcessor { processed: 0 }
    }

    fn process_data(&self, data: &str) -> String {
        data.to_uppercase()
    }
}

impl Actor for DataProcessor {
    type Context = Context<Self>;
}

impl Handler<ProcessData> for DataProcessor {
    type Result = ();

    fn handle(&mut self, msg: ProcessData, _ctx: &mut Context<Self>) -> Self::Result {
        let result = self.process_data(&msg.data);
        println!("Processed: {}", result);
        self.processed += 1;
    }
}

impl Handler<GetStats> for DataProcessor {
    type Result = u64;

    fn handle(&mut self, _msg: GetStats, _ctx: &mut Context<Self>) -> Self::Result {
        self.processed
    }
}

// Usage
#[actix::main]
async fn main() {
    let processor = DataProcessor::new().start();
    
    processor.send(ProcessData {
        data: "hello world".to_string(),
    }).await.unwrap();
    
    let stats = processor.send(GetStats).await.unwrap();
    println!("Stats: {}", stats);
}
```

### 6. Python with Pykka

Pykka brings Akka-style actors to Python.

```python
import pykka

class ProcessData:
    def __init__(self, data):
        self.data = data

class GetStats:
    pass

class DataProcessor(pykka.ThreadingActor):
    def __init__(self):
        super().__init__()
        self.processed = 0

    def on_receive(self, message):
        if isinstance(message, ProcessData):
            result = self.process_data(message.data)
            print(f"Processed: {result}")
            self.processed += 1
            return result
        elif isinstance(message, GetStats):
            return self.processed
        else:
            return super().on_receive(message)

    def process_data(self, data):
        return data.upper()

# Usage
if __name__ == '__main__':
    actor_system = pykka.ActorSystem('DataProcessingSystem')
    
    try:
        processor_ref = actor_system.actor_of(DataProcessor)
        
        processor_ref.tell(ProcessData("hello world"))
        stats = processor_ref.ask(GetStats(), timeout=1)
        print(f"Stats: {stats}")
        
    finally:
        actor_system.shutdown()
```

### 7. JavaScript with Comedy

Comedy provides actor model implementation for Node.js.

```javascript
const { Actor } = require('comedy');

class DataProcessor extends Actor {
  constructor() {
    super();
    this.processed = 0;
  }

  async process(data) {
    const result = this.processData(data);
    console.log(`Processed: ${result}`);
    this.processed++;
    return result;
  }

  async getStats() {
    return this.processed;
  }

  processData(data) {
    return data.toUpperCase();
  }
}

// Usage
async function main() {
  const system = Actor.createSystem();
  const processor = await system.rootActor().createChild(DataProcessor);

  await processor.sendAndReceive('process', 'hello world');
  const stats = await processor.sendAndReceive('getStats');
  console.log(`Stats: ${stats}`);

  await system.destroy();
}

main().catch(console.error);
```

### 8. C# with Akka.NET

Akka.NET brings mature actor model to the .NET ecosystem.

```csharp
using Akka.Actor;
using System;

public class ProcessData
{
    public string Data { get; }
    public ProcessData(string data) => Data = data;
}

public class GetStats { }

public class StatsResponse
{
    public int Processed { get; }
    public StatsResponse(int processed) => Processed = processed;
}

public class DataProcessor : ReceiveActor
{
    private int processed = 0;

    public DataProcessor()
    {
        Receive<ProcessData>(msg =>
        {
            var result = ProcessData(msg.Data);
            Console.WriteLine($"Processed: {result}");
            processed++;
        });

        Receive<GetStats>(_ =>
        {
            Sender.Tell(new StatsResponse(processed));
        });
    }

    private string ProcessData(string data)
    {
        return data.ToUpper();
    }
}

// Usage
class Program
{
    static void Main()
    {
        using var system = ActorSystem.Create("DataProcessingSystem");
        var processor = system.ActorOf<DataProcessor>("processor");

        processor.Tell(new ProcessData("hello world"));
        var response = processor.Ask<StatsResponse>(new GetStats()).Result;
        Console.WriteLine($"Stats: {response.Processed}");
    }
}
```

### 9. Go with Goroutines (Actor-like Pattern)

While Go doesn't have native actors, goroutines + channels provide similar patterns.

```go
package main

import (
    "fmt"
    "strings"
    "sync/atomic"
)

type ProcessData struct {
    Data     string
    Response chan string
}

type GetStats struct {
    Response chan int64
}

type DataProcessor struct {
    input     chan interface{}
    processed int64
}

func NewDataProcessor() *DataProcessor {
    dp := &DataProcessor{
        input: make(chan interface{}, 100),
    }
    go dp.run()
    return dp
}

func (dp *DataProcessor) run() {
    for msg := range dp.input {
        switch m := msg.(type) {
        case ProcessData:
            result := dp.processData(m.Data)
            fmt.Printf("Processed: %s\n", result)
            atomic.AddInt64(&dp.processed, 1)
            m.Response <- result

        case GetStats:
            count := atomic.LoadInt64(&dp.processed)
            m.Response <- count
        }
    }
}

func (dp *DataProcessor) Process(data string) string {
    response := make(chan string)
    dp.input <- ProcessData{Data: data, Response: response}
    return <-response
}

func (dp *DataProcessor) GetStats() int64 {
    response := make(chan int64)
    dp.input <- GetStats{Response: response}
    return <-response
}

func (dp *DataProcessor) processData(data string) string {
    return strings.ToUpper(data)
}

func main() {
    processor := NewDataProcessor()
    
    processor.Process("hello world")
    stats := processor.GetStats()
    fmt.Printf("Stats: %d\n", stats)
}
```

## Proposed Actor Model for Lambda

### Design Philosophy

Lambda's actor model should integrate seamlessly with its functional nature and universal data processing capabilities. Actors in Lambda would be:

1. **Data-centric**: Specialized for processing different data formats
2. **Functional**: Immutable state with functional transformations
3. **Type-safe**: Schema validation for actor messages
4. **Format-aware**: Built-in support for Lambda's extensive format ecosystem

### Syntax Proposal

```lambda
// Actor definition
actor DataProcessor {
  // Private state (immutable)
  state: {
    processed: 0,
    format_stats: {}
  }
  
  // Message handlers
  receive {
    // Process data with automatic format detection
    process(data: Any, format: String?) -> ProcessResult {
      let detected_format = format ?? detect_format(data)
      let parsed = input(data, detected_format)
      let result = transform(parsed)
      
      // Update state functionally
      state = state with {
        processed: state.processed + 1,
        format_stats: update_format_count(state.format_stats, detected_format)
      }
      
      ProcessResult { data: result, format: detected_format }
    }
    
    // Get processing statistics
    get_stats() -> Stats {
      Stats {
        total_processed: state.processed,
        formats: state.format_stats
      }
    }
    
    // Transform data between formats
    convert(data: Any, from_format: String, to_format: String) -> ConvertResult {
      let parsed = input(data, from_format)
      let formatted = output(parsed, to_format)
      
      ConvertResult { 
        data: formatted,
        from: from_format,
        to: to_format
      }
    }
  }
}

// Message type definitions
type ProcessResult = {
  data: Any,
  format: String
}

type Stats = {
  total_processed: Int,
  formats: Map<String, Int>
}

type ConvertResult = {
  data: String,
  from: String,
  to: String
}

// Actor system usage
let system = actor_system()
let processor = system.spawn(DataProcessor)

// Send messages (async)
processor ! process("{ \"name\": \"John\" }", "json")
processor ! process("<person><name>Jane</name></person>", "xml")

// Request-response pattern
let stats = processor ? get_stats()
let converted = processor ? convert(json_data, "json", "yaml")

// Parallel processing
let processors = [
  system.spawn(DataProcessor),
  system.spawn(DataProcessor),
  system.spawn(DataProcessor)
]

let results = parallel_map(data_chunks, (chunk, i) -> {
  processors[i % 3] ? process(chunk)
})
```

### Advanced Features

#### 1. Supervisor Trees

```lambda
supervisor DataProcessingSupervisor {
  strategy: one_for_one
  max_restarts: 3
  
  children: [
    { name: "csv_processor", actor: CSVProcessor, count: 2 },
    { name: "json_processor", actor: JSONProcessor, count: 3 },
    { name: "xml_processor", actor: XMLProcessor, count: 1 }
  ]
}
```

#### 2. Actor Pools

```lambda
let pool = actor_pool(DataProcessor, size: 10)
let results = pool.map(data_items, process)
```

#### 3. Distributed Actors

```lambda
// Remote actor reference
let remote_processor = system.actor_ref("lambda://remote-node/processors/main")
remote_processor ! process(large_dataset, "csv")
```

#### 4. Stream Processing with Actors

```lambda
let stream = input_stream("large_file.csv")
  |> chunk(1000)
  |> actor_map(DataProcessor, process)
  |> collect()
```

#### 5. Mathematical Computation Actors

```lambda
actor MathProcessor {
  state: { computations: 0 }
  
  receive {
    evaluate(expression: String, format: MathFormat) -> MathResult {
      let parsed = parse_math(expression, format)
      let result = evaluate_expression(parsed)
      
      state = state with { computations: state.computations + 1 }
      
      MathResult {
        original: expression,
        parsed: parsed,
        result: result,
        latex: format_math(parsed, 'latex'),
        ascii: format_math(parsed, 'ascii')
      }
    }
    
    symbolic_diff(expression: String, variable: String) -> MathResult {
      let parsed = parse_math(expression, 'latex')
      let derivative = differentiate(parsed, variable)
      
      MathResult {
        original: expression,
        derivative: format_math(derivative, 'latex'),
        simplified: simplify(derivative)
      }
    }
  }
}
```

### Integration with Lambda's Ecosystem

#### 1. HTTP Actors

```lambda
actor HTTPClient {
  state: { requests: 0, cache: {} }
  
  receive {
    get(url: String, format: String?) -> HTTPResult {
      let cached = state.cache[url]
      if cached && !expired(cached) {
        return cached.data
      }
      
      let response = http_get(url)
      let parsed = format ? input(response.body, format) : response.body
      
      state = state with {
        requests: state.requests + 1,
        cache: state.cache with { url: { data: parsed, timestamp: now() } }
      }
      
      HTTPResult { data: parsed, status: response.status }
    }
  }
}
```

#### 2. Schema Validation Actors

```lambda
actor SchemaValidator {
  state: { validations: 0, schemas: {} }
  
  receive {
    validate(data: Any, schema_url: String) -> ValidationResult {
      let schema = load_schema(schema_url)
      let result = validate_against_schema(data, schema)
      
      state = state with { validations: state.validations + 1 }
      
      ValidationResult {
        valid: result.valid,
        errors: result.errors,
        data: data
      }
    }
  }
}
```

### Performance Considerations

1. **Memory Pools**: Actors use Lambda's existing memory pool system
2. **Zero-Copy**: Message passing uses references where possible
3. **Batching**: Support for batch message processing
4. **Backpressure**: Built-in flow control mechanisms

### Error Handling

```lambda
actor RobustProcessor {
  receive {
    process(data: Any) -> Result<ProcessResult, ProcessError> {
      try {
        let result = risky_operation(data)
        Ok(ProcessResult { data: result })
      } catch ParseError(msg) {
        Err(ProcessError { type: "parse", message: msg })
      } catch ValidationError(msg) {
        Err(ProcessError { type: "validation", message: msg })
      }
    }
  }
}
```

This actor model proposal leverages Lambda's existing strengths in data processing, mathematical computation, and format handling while providing the concurrency and fault tolerance benefits of the actor paradigm.
