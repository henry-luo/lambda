package bench

import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/md5"
	cryptorand "crypto/rand"
	"crypto/rsa"
	"crypto/sha1"
	"crypto/sha256"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"math"
	"math/big"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
)

func fib(n int) int {
	if n < 2 {
		return n
	}
	return fib(n-1) + fib(n-2)
}

func fibfp(n float64) float64 {
	if n < 2.0 {
		return n
	}
	return fibfp(n-1.0) + fibfp(n-2.0)
}

func tak(x, y, z int) int {
	if y >= x {
		return z
	}
	return tak(tak(x-1, y, z), tak(y-1, z, x), tak(z-1, x, y))
}

func ack(m, n int) int {
	if m == 0 {
		return n + 1
	}
	if n == 0 {
		return ack(m-1, 1)
	}
	return ack(m-1, ack(m, n-1))
}

func nqueensCount(rows *[8]int, column int) int {
	if column == 8 {
		return 1
	}
	total := 0
	for row := 0; row < 8; row++ {
		valid := true
		for previous := 0; previous < column; previous++ {
			delta := column - previous
			if rows[previous] == row || rows[previous]+delta == row || rows[previous]-delta == row {
				valid = false
				break
			}
		}
		if valid {
			rows[column] = row
			total += nqueensCount(rows, column+1)
		}
	}
	return total
}

func four1(data []float64) {
	n := len(data)
	j := 0
	for i := 0; i < n; i += 2 {
		if i < j {
			data[i], data[j] = data[j], data[i]
			data[i+1], data[j+1] = data[j+1], data[i+1]
		}
		m := n / 2
		for m >= 2 && j >= m {
			j -= m
			m /= 2
		}
		j += m
	}
	for mmax := 2; mmax < n; mmax *= 2 {
		theta := 2.0 * math.Pi / float64(mmax)
		sinHalf := math.Sin(0.5 * theta)
		wpr, wpi := -2.0*sinHalf*sinHalf, math.Sin(theta)
		wr, wi := 1.0, 0.0
		for m := 0; m < mmax; m += 2 {
			for i := m; i < n; i += 2 * mmax {
				jj := i + mmax
				tempr := wr*data[jj] - wi*data[jj+1]
				tempi := wr*data[jj+1] + wi*data[jj]
				data[jj], data[jj+1] = data[i]-tempr, data[i+1]-tempi
				data[i], data[i+1] = data[i]+tempr, data[i+1]+tempi
			}
			wr, wi = wr*wpr-wi*wpi+wr, wi*wpr+wr*wpi+wi
		}
	}
}

func mbrotCount(real, imag, step float64, x, y int) int {
	cr, ci := real+float64(x)*step, imag+float64(y)*step
	zr, zi := cr, ci
	for count := 0; count < 64; count++ {
		zr2, zi2 := zr*zr, zi*zi
		if zr2+zi2 > 16.0 {
			return count
		}
		zr, zi = zr2-zi2+cr, 2.0*zr*zi+ci
	}
	return 64
}

func runR7RS(name string) bool {
	switch name {
	case "fib":
		value := fib(27)
		fmt.Printf("fib: %s\n", pass(value == 196418))
		return value == 196418
	case "fibfp":
		value := fibfp(27.0)
		fmt.Printf("fibfp: %s\n", pass(value == 196418.0))
		return value == 196418.0
	case "tak":
		value := tak(18, 12, 6)
		fmt.Printf("tak: %s\n", pass(value == 7))
		return value == 7
	case "cpstak":
		value := tak(18, 12, 6)
		value = tak(18, 12, 6)
		fmt.Printf("cpstak: %s\n", pass(value == 7))
		return value == 7
	case "sum":
		value := 0
		for repeat := 0; repeat < 100; repeat++ {
			value = 0
			for n := 10000; n >= 0; n-- {
				value += n
			}
		}
		fmt.Printf("sum: %s\n", pass(value == 50005000))
		return value == 50005000
	case "sumfp":
		value := 0.0
		for n := 100000.0; n >= 0.0; n-- {
			value += n
		}
		ok := math.Abs(value-5000050000.0) < 1.0
		fmt.Printf("sumfp: %s\n", pass(ok))
		return ok
	case "nqueens":
		var rows [8]int
		value := nqueensCount(&rows, 0)
		fmt.Printf("nqueens: %s\n", pass(value == 92))
		return value == 92
	case "fft":
		data := make([]float64, 4096)
		four1(data)
		ok := data[0] == 0.0
		fmt.Printf("fft: %s\n", pass(ok))
		return ok
	case "mbrot":
		value := mbrotCount(-1.0, -0.5, 0.005, 0, 0)
		fmt.Printf("mbrot: %s\n", pass(value == 5))
		return value == 5
	case "ack":
		value := ack(3, 8)
		fmt.Printf("ack: %s\n", pass(value == 2045))
		return value == 2045
	default:
		return false
	}
}

func pass(ok bool) string {
	if ok {
		return "PASS"
	}
	return "FAIL"
}

//go:noinline
func roundedFloat64(value float64) float64 {
	return value
}

type listNode struct{ next *listNode }

func makeList(length int) *listNode {
	if length == 0 {
		return nil
	}
	return &listNode{next: makeList(length - 1)}
}

func listLength(node *listNode) int {
	if node == nil {
		return 0
	}
	return 1 + listLength(node.next)
}

func isShorterThan(left, right *listNode) bool {
	for right != nil {
		if left == nil {
			return true
		}
		left, right = left.next, right.next
	}
	return false
}

func listTail(left, middle, right *listNode) *listNode {
	if isShorterThan(middle, left) {
		return listTail(listTail(left.next, middle, right), listTail(middle.next, right, left), listTail(right.next, left, middle))
	}
	return right
}

type storageNode struct {
	children [4]*storageNode
	data     []int
}

var storageRoot *storageNode

func buildStorageTree(depth int, seed *int, count *int) *storageNode {
	*count++
	if depth == 1 {
		*seed = (*seed*1309 + 13849) % 65536
		return &storageNode{data: make([]int, *seed%10+1)}
	}
	node := &storageNode{}
	for index := range node.children {
		node.children[index] = buildStorageTree(depth-1, seed, count)
	}
	return node
}

type nbodySystem struct {
	x, y, z    [5]float64
	vx, vy, vz [5]float64
	mass       [5]float64
}

func (system *nbodySystem) advance() {
	for left := 0; left < 5; left++ {
		for right := left + 1; right < 5; right++ {
			dx, dy, dz := system.x[left]-system.x[right], system.y[left]-system.y[right], system.z[left]-system.z[right]
			distanceSquared := dx*dx + dy*dy + dz*dz
			magnitude := .01 / (distanceSquared * math.Sqrt(distanceSquared))
			system.vx[left] -= dx * system.mass[right] * magnitude
			system.vy[left] -= dy * system.mass[right] * magnitude
			system.vz[left] -= dz * system.mass[right] * magnitude
			system.vx[right] += dx * system.mass[left] * magnitude
			system.vy[right] += dy * system.mass[left] * magnitude
			system.vz[right] += dz * system.mass[left] * magnitude
		}
	}
	for body := 0; body < 5; body++ {
		system.x[body] += .01 * system.vx[body]
		system.y[body] += .01 * system.vy[body]
		system.z[body] += .01 * system.vz[body]
	}
}

func (system *nbodySystem) energy() float64 {
	value := 0.0
	for left := 0; left < 5; left++ {
		value += .5 * system.mass[left] * (system.vx[left]*system.vx[left] + system.vy[left]*system.vy[left] + system.vz[left]*system.vz[left])
		for right := left + 1; right < 5; right++ {
			dx, dy, dz := system.x[left]-system.x[right], system.y[left]-system.y[right], system.z[left]-system.z[right]
			value -= system.mass[left] * system.mass[right] / math.Sqrt(dx*dx+dy*dy+dz*dz)
		}
	}
	return value
}

func makeNBodySystem() nbodySystem {
	const daysPerYear = 365.24
	solarMass := 4 * math.Pi * math.Pi
	system := nbodySystem{
		x:    [5]float64{0, 4.84143144246472090, 8.34336671824457987, 12.8943695621391310, 15.3796971148509165},
		y:    [5]float64{0, -1.16032004402742839, 4.12479856412430479, -15.1111514069918631, -25.9193146099879641},
		z:    [5]float64{0, -.103622044471123109, -.403523417114321381, -.223307578892655734, .179258772950371181},
		vx:   [5]float64{0, .00166007664274403694 * daysPerYear, -.00276742510726862411 * daysPerYear, .00296460137564761618 * daysPerYear, .00268067772490389322 * daysPerYear},
		vy:   [5]float64{0, .00769901118419740425 * daysPerYear, .00499852801234917238 * daysPerYear, .00237847173959480950 * daysPerYear, .00162824170038242295 * daysPerYear},
		vz:   [5]float64{0, -.0000690460016972063023 * daysPerYear, .0000230417297573763926 * daysPerYear, -.0000296589532392949896 * daysPerYear, -.0000951592254519715870 * daysPerYear},
		mass: [5]float64{solarMass, .000954791938424326609 * solarMass, .000285885980666130812 * solarMass, .0000436624404335156298 * solarMass, .0000515138902046611451 * solarMass},
	}
	px, py, pz := 0.0, 0.0, 0.0
	for body := 0; body < 5; body++ {
		px += system.vx[body] * system.mass[body]
		py += system.vy[body] * system.mass[body]
		pz += system.vz[body] * system.mass[body]
	}
	system.vx[0], system.vy[0], system.vz[0] = -px/solarMass, -py/solarMass, -pz/solarMass
	return system
}

const (
	richIdler = iota
	richWorker
	richHandlerA
	richHandlerB
	richDeviceA
	richDeviceB
	richDevicePacket = 0
	richWorkPacket   = 1
	richIdleTask     = 0
	richWorkerTask   = 1
	richHandlerTask  = 2
	richDeviceTask   = 3
)

type richPacket struct {
	link                  *richPacket
	identity, kind, datum int
	data                  [4]int
}

type richTaskState struct{ pending, waiting, held bool }

func richRunning() richTaskState           { return richTaskState{} }
func richWaiting() richTaskState           { return richTaskState{waiting: true} }
func richWaitingWithPacket() richTaskState { return richTaskState{pending: true, waiting: true} }

type richTaskControlBlock struct {
	link               *richTaskControlBlock
	identity, priority int
	input              *richPacket
	state              richTaskState
	data               any
	task               int
}

func (task *richTaskControlBlock) heldOrWaiting() bool {
	return task.state.held || (!task.state.pending && task.state.waiting)
}

func (task *richTaskControlBlock) waitingWithPacket() bool {
	return task.state.pending && task.state.waiting && !task.state.held
}

func (task *richTaskControlBlock) setRunning()           { task.state = richRunning() }
func (task *richTaskControlBlock) setPacketPending()     { task.state = richTaskState{pending: true} }
func (task *richTaskControlBlock) setWaiting()           { task.state = richWaiting() }
func (task *richTaskControlBlock) setWaitingWithPacket() { task.state = richWaitingWithPacket() }

func richAppend(packet, queue *richPacket) *richPacket {
	packet.link = nil
	if queue == nil {
		return packet
	}
	last := queue
	for last.link != nil {
		last = last.link
	}
	last.link = packet
	return queue
}

func (task *richTaskControlBlock) addInput(packet *richPacket, previous *richTaskControlBlock) *richTaskControlBlock {
	if task.input == nil {
		task.input = packet
		task.state.pending = true
		if task.priority > previous.priority {
			return task
		}
		return previous
	}
	task.input = richAppend(packet, task.input)
	return previous
}

type richIdleData struct{ control, count int }
type richWorkerData struct{ destination, count int }
type richHandlerData struct{ workIn, deviceIn *richPacket }
type richDeviceData struct{ pending *richPacket }

type richScheduler struct {
	taskList, current *richTaskControlBlock
	taskTable         [6]*richTaskControlBlock
	currentIdentity   int
	queueCount        int
	holdCount         int
}

func (scheduler *richScheduler) createTask(identity, priority int, input *richPacket, state richTaskState, data any, task int) {
	control := &richTaskControlBlock{link: scheduler.taskList, identity: identity, priority: priority, input: input, state: state, data: data, task: task}
	scheduler.taskList, scheduler.taskTable[identity] = control, control
}

func (scheduler *richScheduler) holdSelf() *richTaskControlBlock {
	scheduler.holdCount++
	scheduler.current.state.held = true
	return scheduler.current.link
}

func (scheduler *richScheduler) markWaiting() *richTaskControlBlock {
	scheduler.current.state.waiting = true
	return scheduler.current
}

func (scheduler *richScheduler) queuePacket(packet *richPacket) *richTaskControlBlock {
	task := scheduler.taskTable[packet.identity]
	if task == nil {
		return nil
	}
	scheduler.queueCount++
	packet.link = nil
	packet.identity = scheduler.currentIdentity
	return task.addInput(packet, scheduler.current)
}

func (scheduler *richScheduler) release(identity int) *richTaskControlBlock {
	task := scheduler.taskTable[identity]
	if task == nil {
		return nil
	}
	task.state.held = false
	if task.priority > scheduler.current.priority {
		return task
	}
	return scheduler.current
}

func (scheduler *richScheduler) runIdle(data *richIdleData) *richTaskControlBlock {
	data.count--
	if data.count == 0 {
		return scheduler.holdSelf()
	}
	if data.control&1 == 0 {
		data.control /= 2
		return scheduler.release(richDeviceA)
	}
	data.control = data.control/2 ^ 53256
	return scheduler.release(richDeviceB)
}

func (scheduler *richScheduler) runWorker(packet *richPacket, data *richWorkerData) *richTaskControlBlock {
	if packet == nil {
		return scheduler.markWaiting()
	}
	if data.destination == richHandlerA {
		data.destination = richHandlerB
	} else {
		data.destination = richHandlerA
	}
	packet.identity, packet.datum = data.destination, 0
	for index := range packet.data {
		data.count++
		if data.count > 26 {
			data.count = 1
		}
		packet.data[index] = 64 + data.count
	}
	return scheduler.queuePacket(packet)
}

func (scheduler *richScheduler) runHandler(packet *richPacket, data *richHandlerData) *richTaskControlBlock {
	if packet != nil {
		if packet.kind == richWorkPacket {
			data.workIn = richAppend(packet, data.workIn)
		} else {
			data.deviceIn = richAppend(packet, data.deviceIn)
		}
	}
	workPacket := data.workIn
	if workPacket == nil {
		return scheduler.markWaiting()
	}
	if workPacket.datum >= 4 {
		data.workIn = workPacket.link
		return scheduler.queuePacket(workPacket)
	}
	devicePacket := data.deviceIn
	if devicePacket == nil {
		return scheduler.markWaiting()
	}
	data.deviceIn = devicePacket.link
	devicePacket.datum = workPacket.data[workPacket.datum]
	workPacket.datum++
	return scheduler.queuePacket(devicePacket)
}

func (scheduler *richScheduler) runDevice(packet *richPacket, data *richDeviceData) *richTaskControlBlock {
	if packet == nil {
		packet = data.pending
		if packet == nil {
			return scheduler.markWaiting()
		}
		data.pending = nil
		return scheduler.queuePacket(packet)
	}
	data.pending = packet
	return scheduler.holdSelf()
}

func (scheduler *richScheduler) runTask(task *richTaskControlBlock) *richTaskControlBlock {
	var packet *richPacket
	if task.waitingWithPacket() {
		packet, task.input = task.input, task.input.link
		if task.input == nil {
			task.setRunning()
		} else {
			task.setPacketPending()
		}
	}
	switch task.task {
	case richIdleTask:
		return scheduler.runIdle(task.data.(*richIdleData))
	case richWorkerTask:
		return scheduler.runWorker(packet, task.data.(*richWorkerData))
	case richHandlerTask:
		return scheduler.runHandler(packet, task.data.(*richHandlerData))
	default:
		return scheduler.runDevice(packet, task.data.(*richDeviceData))
	}
}

func (scheduler *richScheduler) schedule() {
	scheduler.current = scheduler.taskList
	for scheduler.current != nil {
		if scheduler.current.heldOrWaiting() {
			scheduler.current = scheduler.current.link
		} else {
			scheduler.currentIdentity = scheduler.current.identity
			scheduler.current = scheduler.runTask(scheduler.current)
		}
	}
}

func richardsOnce(idleCount, expectedQueueCount, expectedHoldCount int) bool {
	scheduler := &richScheduler{}
	scheduler.createTask(richIdler, 0, nil, richRunning(), &richIdleData{control: 1, count: idleCount}, richIdleTask)
	workerQueue := &richPacket{identity: richWorker, kind: richWorkPacket}
	workerQueue = &richPacket{link: workerQueue, identity: richWorker, kind: richWorkPacket}
	scheduler.createTask(richWorker, 1000, workerQueue, richWaitingWithPacket(), &richWorkerData{destination: richHandlerA}, richWorkerTask)
	queueA := &richPacket{identity: richDeviceA, kind: richDevicePacket}
	queueA = &richPacket{link: queueA, identity: richDeviceA, kind: richDevicePacket}
	queueA = &richPacket{link: queueA, identity: richDeviceA, kind: richDevicePacket}
	scheduler.createTask(richHandlerA, 2000, queueA, richWaitingWithPacket(), &richHandlerData{}, richHandlerTask)
	queueB := &richPacket{identity: richDeviceB, kind: richDevicePacket}
	queueB = &richPacket{link: queueB, identity: richDeviceB, kind: richDevicePacket}
	queueB = &richPacket{link: queueB, identity: richDeviceB, kind: richDevicePacket}
	scheduler.createTask(richHandlerB, 3000, queueB, richWaitingWithPacket(), &richHandlerData{}, richHandlerTask)
	scheduler.createTask(richDeviceA, 4000, nil, richWaiting(), &richDeviceData{}, richDeviceTask)
	scheduler.createTask(richDeviceB, 5000, nil, richWaiting(), &richDeviceData{}, richDeviceTask)
	scheduler.schedule()
	return scheduler.queueCount == expectedQueueCount && scheduler.holdCount == expectedHoldCount
}

type cdVector struct {
	x float64
	y float64
	z float64
}

func (vector cdVector) plus(other cdVector) cdVector {
	return cdVector{vector.x + other.x, vector.y + other.y, vector.z + other.z}
}

func (vector cdVector) minus(other cdVector) cdVector {
	return cdVector{vector.x - other.x, vector.y - other.y, vector.z - other.z}
}

func (vector cdVector) times(amount float64) cdVector {
	return cdVector{vector.x * amount, vector.y * amount, vector.z * amount}
}

func (vector cdVector) dot(other cdVector) float64 {
	return vector.x*other.x + vector.y*other.y + vector.z*other.z
}

type cdMotion struct {
	callSign int
	posOne   cdVector
	posTwo   cdVector
}

func (motion *cdMotion) delta() cdVector {
	return motion.posTwo.minus(motion.posOne)
}

func (motion *cdMotion) intersects(other *cdMotion) bool {
	initialOne, initialTwo := motion.posOne, other.posOne
	vectorOne, vectorTwo := motion.delta(), other.delta()
	delta := vectorTwo.minus(vectorOne)
	a := delta.dot(delta)
	if a != 0 {
		b := 2 * initialOne.minus(initialTwo).dot(vectorOne.minus(vectorTwo))
		c := -1 + initialTwo.minus(initialOne).dot(initialTwo.minus(initialOne))
		discriminant := b*b - 4*a*c
		if discriminant < 0 {
			return false
		}
		v1 := (-b - math.Sqrt(discriminant)) / (2 * a)
		v2 := (-b + math.Sqrt(discriminant)) / (2 * a)
		if !(v1 <= v2 && ((v1 <= 1 && 1 <= v2) || (v1 <= 0 && 0 <= v2) || (0 <= v1 && v2 <= 1))) {
			return false
		}
		v := v1
		if v1 <= 0 {
			v = 0
		}
		positionOne := initialOne.plus(vectorOne.times(v))
		positionTwo := initialTwo.plus(vectorTwo.times(v))
		position := positionOne.plus(positionTwo).times(0.5)
		return position.x >= 0 && position.x <= 1000 && position.y >= 0 && position.y <= 1000 && position.z >= 0 && position.z <= 10
	}
	return initialOne.minus(initialTwo).dot(initialOne.minus(initialTwo)) <= 1
}

type cdVoxel struct {
	x int
	y int
}

func cdVoxelHash(position cdVector) cdVoxel {
	x := int(math.Floor(position.x / 2))
	y := int(math.Floor(position.y / 2))
	if position.x < 0 {
		x--
	}
	if position.y < 0 {
		y--
	}
	return cdVoxel{x, y}
}

func cdInVoxel(voxel cdVoxel, motion *cdMotion) bool {
	voxelX, voxelY := float64(voxel.x*2), float64(voxel.y*2)
	if voxelX > 1000 || voxelX < 0 || voxelY > 1000 || voxelY < 0 {
		return false
	}
	initial, final := motion.posOne, motion.posTwo
	xVelocity, yVelocity := final.x-initial.x, final.y-initial.y
	const radius = 0.5
	lowX, highX := math.Inf(1), math.Inf(1)
	if xVelocity == 0 {
		if voxelX-radius-initial.x < 0 {
			lowX = math.Inf(-1)
		}
		if voxelX+2+radius-initial.x < 0 {
			highX = math.Inf(-1)
		}
	} else {
		lowX = (voxelX - radius - initial.x) / xVelocity
		highX = (voxelX + 2 + radius - initial.x) / xVelocity
	}
	if xVelocity < 0 {
		lowX, highX = highX, lowX
	}
	lowY, highY := math.Inf(1), math.Inf(1)
	if yVelocity == 0 {
		if voxelY-radius-initial.y < 0 {
			lowY = math.Inf(-1)
		}
		if voxelY+2+radius-initial.y < 0 {
			highY = math.Inf(-1)
		}
	} else {
		lowY = (voxelY - radius - initial.y) / yVelocity
		highY = (voxelY + 2 + radius - initial.y) / yVelocity
	}
	if yVelocity < 0 {
		lowY, highY = highY, lowY
	}
	xInRange := (xVelocity == 0 && voxelX <= initial.x+radius && initial.x-radius <= voxelX+2) ||
		(lowX <= 1 && 1 <= highX) || (lowX <= 0 && 0 <= highX) || (0 <= lowX && highX <= 1)
	yInRange := (yVelocity == 0 && voxelY <= initial.y+radius && initial.y-radius <= voxelY+2) ||
		(lowY <= 1 && 1 <= highY) || (lowY <= 0 && 0 <= highY) || (0 <= lowY && highY <= 1)
	return xInRange && yInRange && (xVelocity == 0 || yVelocity == 0 ||
		(lowY <= highX && highX <= highY) || (lowY <= lowX && lowX <= highY) || (lowX <= lowY && highY <= highX))
}

func cdDrawMotion(voxelMap map[cdVoxel][]*cdMotion, seen map[cdVoxel]bool, voxel cdVoxel, motion *cdMotion) {
	if !cdInVoxel(voxel, motion) || seen[voxel] {
		return
	}
	seen[voxel] = true
	voxelMap[voxel] = append(voxelMap[voxel], motion)
	for deltaX := -1; deltaX <= 1; deltaX++ {
		for deltaY := -1; deltaY <= 1; deltaY++ {
			if deltaX != 0 || deltaY != 0 {
				cdDrawMotion(voxelMap, seen, cdVoxel{voxel.x + deltaX, voxel.y + deltaY}, motion)
			}
		}
	}
}

func awfyCD(aircraftCount int) int {
	positions := make(map[int]cdVector, aircraftCount)
	collisionCount := 0
	for frame := 0; frame < 200; frame++ {
		time := float64(frame) / 10
		motions := make([]cdMotion, 0, aircraftCount)
		for aircraft := 0; aircraft < aircraftCount; aircraft += 2 {
			newPositions := [2]cdVector{
				{time, math.Cos(time)*2 + float64(aircraft)*3, 10},
				{time, math.Sin(time)*2 + float64(aircraft)*3, 10},
			}
			for offset, newPosition := range newPositions {
				callSign := aircraft + offset
				oldPosition, exists := positions[callSign]
				if !exists {
					oldPosition = newPosition
				}
				positions[callSign] = newPosition
				motions = append(motions, cdMotion{callSign, oldPosition, newPosition})
			}
		}
		voxelMap := make(map[cdVoxel][]*cdMotion)
		for index := range motions {
			motion := &motions[index]
			cdDrawMotion(voxelMap, make(map[cdVoxel]bool), cdVoxelHash(motion.posOne), motion)
		}
		for _, reduced := range voxelMap {
			if len(reduced) < 2 {
				continue
			}
			for first := 0; first < len(reduced); first++ {
				for second := first + 1; second < len(reduced); second++ {
					if reduced[first].intersects(reduced[second]) {
						collisionCount++
					}
				}
			}
		}
	}
	return collisionCount
}

type havlakBlock struct {
	inEdges  []*havlakBlock
	outEdges []*havlakBlock
}

type havlakCFG struct {
	start  *havlakBlock
	blocks []*havlakBlock
}

func newHavlakCFG() *havlakCFG {
	return &havlakCFG{}
}

func (cfg *havlakCFG) createNode(name int) *havlakBlock {
	for len(cfg.blocks) <= name {
		cfg.blocks = append(cfg.blocks, nil)
	}
	if cfg.blocks[name] == nil {
		cfg.blocks[name] = &havlakBlock{}
		if len(cfg.blocks) == 1 {
			cfg.start = cfg.blocks[name]
		}
	}
	return cfg.blocks[name]
}

func (cfg *havlakCFG) addEdge(from, to int) {
	fromBlock, toBlock := cfg.createNode(from), cfg.createNode(to)
	fromBlock.outEdges = append(fromBlock.outEdges, toBlock)
	toBlock.inEdges = append(toBlock.inEdges, fromBlock)
}

type havlakLoop struct {
	parent       *havlakLoop
	children     []*havlakLoop
	blocks       map[*havlakBlock]bool
	isRoot       bool
	nestingLevel int
	depthLevel   int
}

func newHavlakLoop(header *havlakBlock) *havlakLoop {
	loop := &havlakLoop{blocks: make(map[*havlakBlock]bool)}
	if header != nil {
		loop.blocks[header] = true
	}
	return loop
}

func (loop *havlakLoop) setParent(parent *havlakLoop) {
	loop.parent = parent
	parent.children = append(parent.children, loop)
}

type havlakLoopGraph struct {
	loops []*havlakLoop
	root  *havlakLoop
}

func newHavlakLoopGraph() *havlakLoopGraph {
	root := newHavlakLoop(nil)
	root.isRoot = true
	return &havlakLoopGraph{loops: []*havlakLoop{root}, root: root}
}

func (graph *havlakLoopGraph) createLoop(header *havlakBlock) *havlakLoop {
	loop := newHavlakLoop(header)
	graph.loops = append(graph.loops, loop)
	return loop
}

func (graph *havlakLoopGraph) calculateNestingLevel() {
	for _, loop := range graph.loops {
		if !loop.isRoot && loop.parent == nil {
			loop.setParent(graph.root)
		}
	}
	var calculate func(*havlakLoop, int)
	calculate = func(loop *havlakLoop, depth int) {
		loop.depthLevel = depth
		for _, child := range loop.children {
			calculate(child, depth+1)
			loop.nestingLevel = max(loop.nestingLevel, 1+child.nestingLevel)
		}
	}
	calculate(graph.root, 0)
}

type havlakUnionFindNode struct {
	parent    *havlakUnionFindNode
	block     *havlakBlock
	dfsNumber int
	loop      *havlakLoop
}

func (node *havlakUnionFindNode) init(block *havlakBlock, number int) {
	node.parent = node
	node.block = block
	node.dfsNumber = number
}

func (node *havlakUnionFindNode) findSet() *havlakUnionFindNode {
	if node.parent != node {
		node.parent = node.parent.findSet()
	}
	return node.parent
}

func (node *havlakUnionFindNode) union(parent *havlakUnionFindNode) {
	node.parent = parent
}

const (
	havlakNonHeader = iota
	havlakReducible
	havlakSelf
	havlakIrreducible
	havlakDead
)

type havlakFinder struct {
	cfg          *havlakCFG
	graph        *havlakLoopGraph
	number       map[*havlakBlock]int
	last         []int
	header       []int
	kind         []int
	nodes        []*havlakUnionFindNode
	backPreds    [][]int
	nonBackPreds []map[int]bool
}

func newHavlakFinder(cfg *havlakCFG, graph *havlakLoopGraph) *havlakFinder {
	return &havlakFinder{cfg: cfg, graph: graph}
}

func (finder *havlakFinder) isAncestor(w, v int) bool {
	return w <= v && v <= finder.last[w]
}

func (finder *havlakFinder) doDFS(block *havlakBlock, current int) int {
	finder.nodes[current].init(block, current)
	finder.number[block] = current
	lastID := current
	for _, target := range block.outEdges {
		if finder.number[target] == math.MaxInt {
			lastID = finder.doDFS(target, lastID+1)
		}
	}
	finder.last[current] = lastID
	return lastID
}

func (finder *havlakFinder) findLoops() {
	if finder.cfg.start == nil {
		return
	}
	size := len(finder.cfg.blocks)
	finder.number = make(map[*havlakBlock]int, size)
	finder.last = make([]int, size)
	finder.header = make([]int, size)
	finder.kind = make([]int, size)
	finder.nodes = make([]*havlakUnionFindNode, size)
	finder.backPreds = make([][]int, size)
	finder.nonBackPreds = make([]map[int]bool, size)
	for index, block := range finder.cfg.blocks {
		finder.number[block] = math.MaxInt
		finder.nodes[index] = &havlakUnionFindNode{}
		finder.nonBackPreds[index] = make(map[int]bool)
	}
	finder.doDFS(finder.cfg.start, 0)
	for w := 0; w < size; w++ {
		finder.kind[w] = havlakNonHeader
		block := finder.nodes[w].block
		if block == nil {
			finder.kind[w] = havlakDead
			continue
		}
		for _, predecessor := range block.inEdges {
			v := finder.number[predecessor]
			if v == math.MaxInt {
				continue
			}
			if finder.isAncestor(w, v) {
				finder.backPreds[w] = append(finder.backPreds[w], v)
			} else {
				finder.nonBackPreds[w][v] = true
			}
		}
	}
	finder.header[0] = 0
	for w := size - 1; w >= 0; w-- {
		block := finder.nodes[w].block
		nodePool := make([]*havlakUnionFindNode, 0)
		inPool := make(map[*havlakUnionFindNode]bool)
		if block != nil {
			for _, v := range finder.backPreds[w] {
				if v == w {
					finder.kind[w] = havlakSelf
					continue
				}
				node := finder.nodes[v].findSet()
				if !inPool[node] {
					inPool[node] = true
					nodePool = append(nodePool, node)
				}
			}
			workList := append([]*havlakUnionFindNode(nil), nodePool...)
			if len(nodePool) != 0 {
				finder.kind[w] = havlakReducible
			}
			for len(workList) != 0 {
				x := workList[0]
				workList = workList[1:]
				if len(finder.nonBackPreds[x.dfsNumber]) > 32*1024 {
					return
				}
				for predecessor := range finder.nonBackPreds[x.dfsNumber] {
					ydash := finder.nodes[predecessor].findSet()
					if !finder.isAncestor(w, ydash.dfsNumber) {
						finder.kind[w] = havlakIrreducible
						finder.nonBackPreds[w][ydash.dfsNumber] = true
					} else if ydash.dfsNumber != w && !inPool[ydash] {
						inPool[ydash] = true
						nodePool = append(nodePool, ydash)
						workList = append(workList, ydash)
					}
				}
			}
		}
		if len(nodePool) == 0 && finder.kind[w] != havlakSelf {
			continue
		}
		loop := finder.graph.createLoop(block)
		finder.nodes[w].loop = loop
		for _, node := range nodePool {
			finder.header[node.dfsNumber] = w
			node.union(finder.nodes[w])
			if node.loop != nil {
				node.loop.setParent(loop)
			} else {
				loop.blocks[node.block] = true
			}
		}
	}
}

type havlakApp struct {
	cfg   *havlakCFG
	graph *havlakLoopGraph
}

func newHavlakApp() *havlakApp {
	app := &havlakApp{cfg: newHavlakCFG(), graph: newHavlakLoopGraph()}
	app.cfg.createNode(0)
	return app
}

func (app *havlakApp) buildDiamond(start int) int {
	app.cfg.addEdge(start, start+1)
	app.cfg.addEdge(start, start+2)
	app.cfg.addEdge(start+1, start+3)
	app.cfg.addEdge(start+2, start+3)
	return start + 3
}

func (app *havlakApp) buildStraight(start, count int) int {
	for index := 0; index < count; index++ {
		app.cfg.addEdge(start+index, start+index+1)
	}
	return start + count
}

func (app *havlakApp) buildBaseLoop(from int) int {
	header := app.buildStraight(from, 1)
	diamondOne := app.buildDiamond(header)
	d11 := app.buildStraight(diamondOne, 1)
	diamondTwo := app.buildDiamond(d11)
	footer := app.buildStraight(diamondTwo, 1)
	app.cfg.addEdge(diamondTwo, d11)
	app.cfg.addEdge(diamondOne, header)
	app.cfg.addEdge(footer, from)
	return app.buildStraight(footer, 1)
}

func (app *havlakApp) constructSimpleCFG() {
	app.cfg.createNode(0)
	app.buildBaseLoop(0)
	app.cfg.createNode(1)
	app.cfg.addEdge(0, 2)
}

func (app *havlakApp) constructCFG(parallelLoops, nestedParallelLoops, baseLoops int) {
	n := 2
	for parallel := 0; parallel < parallelLoops; parallel++ {
		app.cfg.createNode(n + 1)
		app.cfg.addEdge(2, n+1)
		n++
		for nested := 0; nested < nestedParallelLoops; nested++ {
			top := n
			n = app.buildStraight(n, 1)
			for base := 0; base < baseLoops; base++ {
				n = app.buildBaseLoop(n)
			}
			bottom := app.buildStraight(n, 1)
			app.cfg.addEdge(n, top)
			n = bottom
		}
		app.cfg.addEdge(n, 1)
	}
}

func awfyHavlak() (int, int) {
	app := newHavlakApp()
	app.constructSimpleCFG()
	for iteration := 0; iteration < 1; iteration++ {
		newHavlakFinder(app.cfg, app.graph).findLoops()
	}
	app.constructCFG(10, 10, 5)
	newHavlakFinder(app.cfg, app.graph).findLoops()
	for iteration := 0; iteration < 1; iteration++ {
		newHavlakFinder(app.cfg, newHavlakLoopGraph()).findLoops()
	}
	app.graph.calculateNestingLevel()
	return len(app.graph.loops), len(app.cfg.blocks)
}

type cowDocumentRow struct {
	id       int
	revision int
	text     string
}

type cowDocument struct {
	rows      []*cowDocumentRow
	shared    bool
	rowCloned []bool
}

func newCOWDocument(count int) *cowDocument {
	document := &cowDocument{rows: make([]*cowDocumentRow, count)}
	for index := range document.rows {
		document.rows[index] = &cowDocumentRow{id: index, text: "draft"}
	}
	return document
}

func (document *cowDocument) snapshot() *cowDocument {
	// retain the shared root until the next write so the snapshot keeps its original leaves.
	document.shared = true
	return &cowDocument{rows: document.rows, shared: true}
}

func (document *cowDocument) ensureWritableRow(index int) {
	if document.shared {
		document.rows = append([]*cowDocumentRow(nil), document.rows...)
		document.rowCloned = make([]bool, len(document.rows))
		document.shared = false
	}
	if document.rowCloned == nil {
		document.rowCloned = make([]bool, len(document.rows))
	}
	if !document.rowCloned[index] {
		row := *document.rows[index]
		document.rows[index] = &row
		document.rowCloned[index] = true
	}
}

func (document *cowDocument) edit(rounds int) {
	for index := 0; index < rounds; index++ {
		slot := index % len(document.rows)
		document.ensureWritableRow(slot)
		document.rows[slot].text = "published"
		document.rows[slot].revision = index
	}
}

func (document *cowDocument) html() string {
	var output strings.Builder
	output.WriteString("<article class=\"draft\">")
	for _, row := range document.rows {
		fmt.Fprintf(&output, "<p id=\"%d\" revision=\"%d\">%s</p>", row.id, row.revision, row.text)
	}
	output.WriteString("</article>")
	return output.String()
}

func runCOWDocumentEdit() bool {
	document := newCOWDocument(256)
	snapshot := document.snapshot()
	document.edit(2048)
	html := document.html()
	ok := len(snapshot.rows) == 256 && snapshot.rows[0].text == "draft" && snapshot.rows[0].revision == 0 &&
		document.rows[0].text == "published" && document.rows[0].revision == 1792 && len(html) > 0
	fmt.Printf("document-edit: %s (snapshot=%d revision=%d html=%d)\n", pass(ok), len(snapshot.rows), document.rows[0].revision, len(html))
	return ok
}

func runAWFY(name string) bool {
	switch name {
	case "list":
		result := listLength(listTail(makeList(15), makeList(10), makeList(6)))
		ok := result == 10
		fmt.Printf("List: %s\n", pass(ok))
		return ok
	case "storage":
		seed, count := 74755, 0
		storageRoot = buildStorageTree(7, &seed, &count)
		ok := count == 5461
		fmt.Printf("Storage: %s\n", pass(ok))
		return ok
	case "nbody":
		system := makeNBodySystem()
		for iteration := 0; iteration < 36000; iteration++ {
			system.advance()
		}
		ok := int(math.Floor(-system.energy()*10000000)) == 1690142
		fmt.Printf("NBody: %s\n", pass(ok))
		return ok
	case "json":
		fixture, err := awfyJSONFixture()
		if err != nil {
			return false
		}
		var value struct {
			Head       json.RawMessage   `json:"head"`
			Operations []json.RawMessage `json:"operations"`
		}
		err = json.Unmarshal(fixture, &value)
		var head map[string]json.RawMessage
		ok := err == nil && json.Unmarshal(value.Head, &head) == nil && len(value.Operations) == 156
		fmt.Printf("Json: %s\n", pass(ok))
		return ok
	case "richards":
		ok := true
		for iteration := 0; iteration < 50; iteration++ {
			ok = ok && richardsOnce(10000, 23246, 9297)
		}
		fmt.Printf("Richards: %s\n", pass(ok))
		return ok
	case "deltablue":
		ok := awfyDeltaBlue()
		fmt.Printf("DeltaBlue: %s\n", pass(ok))
		return ok
	case "cd":
		collisions := awfyCD(100)
		ok := collisions == 4305
		fmt.Printf("CD: %s (collisions=%d)\n", pass(ok), collisions)
		return ok
	case "havlak":
		loops, nodes := awfyHavlak()
		ok := loops == 1605 && nodes == 5213
		fmt.Printf("Havlak: %s (loops=%d nodes=%d)\n", pass(ok), loops, nodes)
		return ok
	case "sieve":
		flags := make([]bool, 5000)
		for i := range flags {
			flags[i] = true
		}
		count := 0
		for i := 2; i <= 5000; i++ {
			if flags[i-1] {
				count++
				for k := i + i; k <= 5000; k += i {
					flags[k-1] = false
				}
			}
		}
		fmt.Printf("Sieve: %s\n", pass(count == 669))
		return count == 669
	case "permute":
		values, count := [6]int{}, 0
		var visit func(int)
		visit = func(n int) {
			count++
			if n == 0 {
				return
			}
			n--
			visit(n)
			for i := n; i >= 0; i-- {
				values[n], values[i] = values[i], values[n]
				visit(n)
				values[n], values[i] = values[i], values[n]
			}
		}
		visit(6)
		fmt.Printf("Permute: %s\n", pass(count == 8660))
		return count == 8660
	case "queens":
		var rows [8]bool
		var d1, d2 [16]bool
		var place func(int) bool
		place = func(column int) bool {
			for row := 0; row < 8; row++ {
				a, b := column+row, column-row+7
				if !rows[row] && !d1[a] && !d2[b] {
					rows[row], d1[a], d2[b] = true, true, true
					if column == 7 || place(column+1) {
						return true
					}
					rows[row], d1[a], d2[b] = false, false, false
				}
			}
			return false
		}
		ok := true
		for i := 0; i < 10; i++ {
			rows = [8]bool{}
			d1 = [16]bool{}
			d2 = [16]bool{}
			ok = ok && place(0)
		}
		fmt.Printf("Queens: %s\n", pass(ok))
		return ok
	case "towers":
		moves := 0
		var move func(int, int, int)
		move = func(disks, from, to int) {
			if disks == 1 {
				moves++
				return
			}
			other := 3 - from - to
			move(disks-1, from, other)
			moves++
			move(disks-1, other, to)
		}
		move(13, 0, 1)
		fmt.Printf("Towers: %s\n", pass(moves == 8191))
		return moves == 8191
	case "bounce":
		seed := 74755
		next := func() int { seed = (seed*1309 + 13849) % 65536; return seed }
		bx, by, bxv, byv := [100]int{}, [100]int{}, [100]int{}, [100]int{}
		for i := 0; i < 100; i++ {
			bx[i] = next() % 500
			by[i] = next() % 500
			bxv[i] = next()%300 - 150
			byv[i] = next()%300 - 150
		}
		bounces := 0
		abs := func(v int) int {
			if v < 0 {
				return -v
			}
			return v
		}
		for step := 0; step < 50; step++ {
			for i := 0; i < 100; i++ {
				bounced := false
				bx[i] += bxv[i]
				by[i] += byv[i]
				if bx[i] > 500 {
					bx[i] = 500
					bxv[i] = -abs(bxv[i])
					bounced = true
				}
				if bx[i] < 0 {
					bx[i] = 0
					bxv[i] = abs(bxv[i])
					bounced = true
				}
				if by[i] > 500 {
					by[i] = 500
					byv[i] = -abs(byv[i])
					bounced = true
				}
				if by[i] < 0 {
					by[i] = 0
					byv[i] = abs(byv[i])
					bounced = true
				}
				if bounced {
					bounces++
				}
			}
		}
		fmt.Printf("Bounce: %s\n", pass(bounces == 1331))
		return bounces == 1331
	case "mandelbrot":
		sum, byteAcc, bits := 0, 0, 0
		for y := 0; y < 500; y++ {
			ci := 2.0*float64(y)/500 - 1
			for x := 0; x < 500; x++ {
				zrzr, zi, zizi := 0.0, 0.0, 0.0
				cr := 2.0*float64(x)/500 - 1.5
				escape := 0
				for z := 0; z < 50; z++ {
					zr := zrzr - zizi + cr
					// preserve the source's rounded product before adding ci; Go fuses this on arm64.
					zi = roundedFloat64(2*zr*zi) + ci
					zrzr = zr * zr
					zizi = zi * zi
					if zrzr+zizi > 4 {
						escape = 1
						break
					}
				}
				byteAcc = (byteAcc << 1) + escape
				bits++
				if bits == 8 {
					sum ^= byteAcc
					byteAcc, bits = 0, 0
				} else if x == 499 {
					sum ^= byteAcc << (8 - bits)
					byteAcc, bits = 0, 0
				}
			}
		}
		fmt.Printf("Mandelbrot: %s\n", pass(sum == 191))
		return sum == 191
	default:
		return false
	}
}

func levenshtein(left, right string) int {
	previous := make([]int, len(right)+1)
	current := make([]int, len(right)+1)
	for index := range previous {
		previous[index] = index
	}
	for i := 1; i <= len(left); i++ {
		current[0] = i
		for j := 1; j <= len(right); j++ {
			cost := 1
			if left[i-1] == right[j-1] {
				cost = 0
			}
			current[j] = minInt(previous[j]+1, current[j-1]+1, previous[j-1]+cost)
		}
		previous, current = current, previous
	}
	return previous[len(right)]
}

func minInt(values ...int) int {
	result := values[0]
	for _, value := range values[1:] {
		if value < result {
			result = value
		}
	}
	return result
}

func floorDiv(value, divisor int) int {
	if value < 0 {
		return -((-value + divisor - 1) / divisor)
	}
	return value / divisor
}

func runKostya(name string) bool {
	switch name {
	case "primes":
		flags := make([]bool, 1000001)
		for i := range flags {
			flags[i] = true
		}
		flags[0], flags[1] = false, false
		for i := 2; i*i <= 1000000; i++ {
			if flags[i] {
				for j := i * i; j <= 1000000; j += i {
					flags[j] = false
				}
			}
		}
		count := 0
		for _, prime := range flags {
			if prime {
				count++
			}
		}
		ok := count == 78498
		fmt.Printf("primes: %s (%d)\n", pass(ok), count)
		return ok
	case "collatz":
		longest, start := 0, 0
		for candidate := 1; candidate < 1000000; candidate++ {
			value := uint64(candidate)
			length := 1
			for value != 1 {
				if value%2 == 0 {
					value /= 2
				} else {
					value = 3*value + 1
				}
				length++
			}
			if length > longest {
				longest, start = length, candidate
			}
		}
		ok := start == 837799
		fmt.Printf("collatz: %s (start=%d)\n", pass(ok), start)
		return ok
	case "base64":
		input := strings.Repeat("a", 10000)
		encoded := ""
		for i := 0; i < 100; i++ {
			encoded = base64.StdEncoding.EncodeToString([]byte(input))
		}
		decoded, err := base64.StdEncoding.DecodeString(encoded)
		ok := err == nil && string(decoded) == input
		fmt.Printf("base64: encoded_len=%d decoded_len=%d\nbase64: %s\n", len(encoded), len(decoded), pass(ok))
		return ok
	case "levenshtein":
		a, b := strings.Repeat("a", 500), strings.Repeat("b", 500)
		ab, ba := strings.Repeat("ab", 200), strings.Repeat("ba", 200)
		ok := levenshtein("kitten", "sitting") == 3 && levenshtein("saturday", "sunday") == 3 && levenshtein(a, b) == 500 && levenshtein(ab, ba) == 2
		fmt.Printf("levenshtein: %s\n", pass(ok))
		return ok
	case "matmul":
		const size = 200
		a, b, c := make([]float64, size*size), make([]float64, size*size), make([]float64, size*size)
		seed := 42
		next := func() int { seed = (seed*1664525 + 1013904223) % 1000000; return seed }
		for i := range a {
			a[i] = float64(next()%2000)/1000 - 1
			b[i] = float64(next()%2000)/1000 - 1
		}
		for i := 0; i < size; i++ {
			for j := 0; j < size; j++ {
				for k := 0; k < size; k++ {
					c[i*size+j] += a[i*size+k] * b[k*size+j]
				}
			}
		}
		total := 0.0
		for _, v := range c {
			total += v
		}
		fmt.Printf("matmul: sum=%d\nmatmul: DONE\n", int(math.Floor(total)))
		return true
	case "brainfuck":
		program := "++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]>>.>---.+++++++..+++.>>.<-.<.+++.------.--------.>>+.>++."
		jumps := map[int]int{}
		stack := []int{}
		for i, ch := range []byte(program) {
			if ch == '[' {
				stack = append(stack, i)
			}
			if ch == ']' {
				open := stack[len(stack)-1]
				stack = stack[:len(stack)-1]
				jumps[i], jumps[open] = open, i
			}
		}
		output := ""
		for repeat := 0; repeat < 10000; repeat++ {
			tape := [30000]byte{}
			data, instruction := 0, 0
			var built strings.Builder
			for instruction < len(program) {
				switch program[instruction] {
				case '+':
					tape[data]++
				case '-':
					tape[data]--
				case '>':
					data++
				case '<':
					data--
				case '.':
					built.WriteByte(tape[data])
				case '[':
					if tape[data] == 0 {
						instruction = jumps[instruction]
					}
				case ']':
					if tape[data] != 0 {
						instruction = jumps[instruction]
					}
				}
				instruction++
			}
			output = built.String()
		}
		ok := output == "Hello World!\n"
		fmt.Print(output)
		return ok
	case "json_gen":
		length := 0
		for repeat := 0; repeat < 10; repeat++ {
			seed := 42
			next := func() int { seed = (seed*1664525 + 1013904223) % 1000000; return seed }
			var text strings.Builder
			text.WriteByte('[')
			for i := 0; i < 1000; i++ {
				if i > 0 {
					text.WriteByte(',')
				}
				id := next() % 10000
				x := floorDiv(next()%20000-10000, 100)
				y := floorDiv(next()%20000-10000, 100)
				score := next() % 100
				fmt.Fprintf(&text, "{\"id\":%d,\"score\":%d,\"coord\":{\"x\":%d,\"y\":%d},\"active\":true}", id, score, x, y)
			}
			text.WriteByte(']')
			length = text.Len()
		}
		ok := length == 61626
		fmt.Printf("json_gen: length=%d\njson_gen: %s\n", length, pass(ok))
		return ok
	default:
		return false
	}
}

type treeNode struct{ left, right *treeNode }

func makeTree(depth int) *treeNode {
	if depth == 0 {
		return &treeNode{}
	}
	return &treeNode{makeTree(depth - 1), makeTree(depth - 1)}
}
func checkTree(node *treeNode) int {
	if node.left == nil {
		return 1
	}
	return 1 + checkTree(node.left) + checkTree(node.right)
}

type symbolicExpr struct {
	tag         int
	left, right *symbolicExpr
}

func derivative(expr *symbolicExpr) *symbolicExpr {
	if expr.tag == 0 {
		return &symbolicExpr{tag: 0}
	}
	if expr.tag == 1 {
		return &symbolicExpr{tag: 0}
	}
	if expr.tag == 2 {
		return &symbolicExpr{tag: 2, left: derivative(expr.left), right: derivative(expr.right)}
	}
	leftDerivative, rightDerivative := derivative(expr.left), derivative(expr.right)
	return &symbolicExpr{tag: 2,
		left:  &symbolicExpr{tag: 3, left: expr.left, right: rightDerivative},
		right: &symbolicExpr{tag: 3, left: leftDerivative, right: expr.right}}
}

func expressionNodeCount(expr *symbolicExpr) int {
	if expr.tag < 2 {
		return 1
	}
	return 1 + expressionNodeCount(expr.left) + expressionNodeCount(expr.right)
}

func makeDerivativeExpression() *symbolicExpr {
	constant := func() *symbolicExpr { return &symbolicExpr{tag: 0} }
	variable := func() *symbolicExpr { return &symbolicExpr{tag: 1} }
	m1 := &symbolicExpr{tag: 3, left: constant(), right: variable()}
	m2 := &symbolicExpr{tag: 3, left: m1, right: variable()}
	m3 := &symbolicExpr{tag: 3, left: m2, right: variable()}
	m4 := &symbolicExpr{tag: 3, left: constant(), right: variable()}
	m5 := &symbolicExpr{tag: 3, left: m4, right: variable()}
	a1 := &symbolicExpr{tag: 2, left: m3, right: m5}
	a2 := &symbolicExpr{tag: 2, left: a1, right: variable()}
	return &symbolicExpr{tag: 2, left: a2, right: constant()}
}

func multiset2(count int) int { return count * (count + 1) / 2 }
func multiset3(count int) int { return count * (count + 1) * (count + 2) / 6 }
func multiset4(count int) int { return count * (count + 1) * (count + 2) * (count + 3) / 24 }

func paraffinRadicals(counts []int, size int) int {
	total, target := 0, size-1
	for first := 0; first*3 <= target; first++ {
		for second := first; first+second*2 <= target; second++ {
			third := target - first - second
			if third < second {
				continue
			}
			a, b, c := counts[first], counts[second], counts[third]
			switch {
			case first == second && second == third:
				total += multiset3(a)
			case first == second:
				total += multiset2(a) * c
			case second == third:
				total += a * multiset2(b)
			default:
				total += a * b * c
			}
		}
	}
	return total
}

func paraffinCCP(counts []int, size int) int {
	total, sum, maximum := 0, size-1, (size-1)/2
	for first := 0; first*4 <= sum; first++ {
		for second := first; first+second*3 <= sum; second++ {
			remaining := sum - first - second
			for third := second; third*2 <= remaining; third++ {
				fourth := remaining - third
				if fourth < third || fourth > maximum {
					continue
				}
				a, b, c, d := counts[first], counts[second], counts[third], counts[fourth]
				switch {
				case first == second && second == third && third == fourth:
					total += multiset4(a)
				case first == second && second == third:
					total += multiset3(a) * d
				case second == third && third == fourth:
					total += a * multiset3(b)
				case first == second && third == fourth:
					total += multiset2(a) * multiset2(c)
				case first == second:
					total += multiset2(a) * c * d
				case second == third:
					total += a * multiset2(b) * d
				case third == fourth:
					total += a * b * multiset2(c)
				default:
					total += a * b * c * d
				}
			}
		}
	}
	return total
}

func paraffinCount(size int) int {
	half := size / 2
	counts := make([]int, half+1)
	counts[0] = 1
	for radicalSize := 1; radicalSize <= half; radicalSize++ {
		counts[radicalSize] = paraffinRadicals(counts, radicalSize)
	}
	bondCentered := 0
	if size%2 == 0 {
		bondCentered = multiset2(counts[half])
	}
	return bondCentered + paraffinCCP(counts, size)
}

func runLarceny(name string) bool {
	switch name {
	case "deriv":
		result := 0
		for iteration := 0; iteration < 5000; iteration++ {
			result = expressionNodeCount(derivative(makeDerivativeExpression()))
		}
		ok := result == 45
		fmt.Printf("deriv: %s\n", pass(ok))
		return ok
	case "array1":
		values := make([]int, 10000)
		for i := range values {
			values[i] = i
		}
		total := 0
		for repeat := 0; repeat < 100; repeat++ {
			total = 0
			for _, value := range values {
				total += value
			}
		}
		ok := total == 49995000
		fmt.Printf("array1: %s\n", pass(ok))
		return ok
	case "diviter":
		divide := func(x, y int) int {
			q := 0
			for x >= y {
				x -= y
				q++
			}
			return q
		}
		modulus := func(x, y int) int {
			for x >= y {
				x -= y
			}
			return x
		}
		result := 0
		for i := 0; i < 1000; i++ {
			result += divide(1000000, 2) - modulus(1000000, 2)
		}
		ok := result == 500000000
		fmt.Printf("diviter: %s\n", pass(ok))
		return ok
	case "divrec":
		var divide func(int, int, int) int
		divide = func(x, y, q int) int {
			if x < y {
				return q
			}
			return divide(x-y, y, q+1)
		}
		var modulus func(int, int) int
		modulus = func(x, y int) int {
			if x < y {
				return x
			}
			return modulus(x-y, y)
		}
		result := 0
		for i := 0; i < 1000; i++ {
			result += divide(1000, 2, 0) - modulus(1000, 2)
		}
		ok := result == 500000
		fmt.Printf("divrec: %s\n", pass(ok))
		return ok
	case "primes":
		flags := make([]bool, 1000001)
		for i := range flags {
			flags[i] = true
		}
		flags[0], flags[1] = false, false
		for i := 2; i*i <= 1000000; i++ {
			if flags[i] {
				for j := i * i; j <= 1000000; j += i {
					flags[j] = false
				}
			}
		}
		count := 0
		for _, v := range flags {
			if v {
				count++
			}
		}
		ok := count == 78498
		fmt.Printf("primes: %s\n", pass(ok))
		return ok
	case "quicksort":
		values := make([]int, 5000)
		seed := 42
		for i := range values {
			seed = (seed*1664525 + 1013904223) % 1000000
			values[i] = seed
		}
		var sort func(int, int)
		sort = func(lo, hi int) {
			if lo >= hi {
				return
			}
			pivot := values[hi]
			at := lo
			for i := lo; i < hi; i++ {
				if values[i] <= pivot {
					values[i], values[at] = values[at], values[i]
					at++
				}
			}
			values[at], values[hi] = values[hi], values[at]
			sort(lo, at-1)
			sort(at+1, hi)
		}
		sort(0, len(values)-1)
		ok := true
		for i := 1; i < len(values); i++ {
			ok = ok && values[i] >= values[i-1]
		}
		fmt.Printf("quicksort: %s\n", pass(ok))
		return ok
	case "puzzle":
		var columns [10]bool
		var forward, back [20]bool
		var solve func(int) int
		solve = func(row int) int {
			if row == 10 {
				return 1
			}
			count := 0
			for column := 0; column < 10; column++ {
				a, b := row+column, row-column+9
				if !columns[column] && !forward[a] && !back[b] {
					columns[column], forward[a], back[b] = true, true, true
					count += solve(row + 1)
					columns[column], forward[a], back[b] = false, false, false
				}
			}
			return count
		}
		result := solve(0)
		ok := result == 724
		fmt.Printf("puzzle: %s\n", pass(ok))
		return ok
	case "gcbench":
		stretch := checkTree(makeTree(15))
		long := makeTree(14)
		ok := stretch == 65535 && checkTree(long) == 32767
		for depth := 4; depth <= 14; depth += 2 {
			iterations := 1 << (14 - depth + 4)
			total := 0
			for i := 0; i < iterations; i++ {
				total += checkTree(makeTree(depth))
			}
			ok = ok && total == iterations*((1<<(depth+1))-1)
		}
		fmt.Printf("gcbench: %s\n", pass(ok))
		return ok
	case "ray":
		spheres := [4][3]float64{{0, 0, 5}, {-2, 0, 5}, {2, 0, 5}, {0, 2, 5}}
		hits := 0
		for py := 0; py < 100; py++ {
			for px := 0; px < 100; px++ {
				dx, dy, dz := (float64(px)-50)/50, (float64(py)-50)/50, 1.0
				l := math.Sqrt(dx*dx + dy*dy + dz*dz)
				dx, dy, dz = dx/l, dy/l, dz/l
				hit := false
				for _, s := range spheres {
					ex, ey, ez := -s[0], -s[1], -s[2]
					b := 2 * (ex*dx + ey*dy + ez*dz)
					disc := b*b - 4*(ex*ex+ey*ey+ez*ez-1)
					if disc >= 0 && (-b-math.Sqrt(disc))/2 > 0.001 {
						hit = true
					}
				}
				if hit {
					hits++
				}
			}
		}
		ok := hits == 1392
		fmt.Printf("ray: hits=%d\nray: %s\n", hits, pass(ok))
		return ok
	case "pnpoly":
		xs := [20]float64{0, 1, 1, 0, 0, 1, -.5, -1, -1, -2, -2.5, -2, -1.5, -.5, .5, 1, .5, 0, -.5, -1}
		ys := [20]float64{0, 0, 1, 1, 2, 3, 2, 3, 0, -.5, .5, 1.5, 2, 3, 3, 2, 1, .5, -1, -1}
		inside := func(x, y float64) bool {
			value := false
			for i, j := 0, 19; i < 20; j, i = i, i+1 {
				if (ys[i] > y) != (ys[j] > y) && x < (xs[j]-xs[i])*(y-ys[i])/(ys[j]-ys[i])+xs[i] {
					value = !value
				}
			}
			return value
		}
		count := 0
		for ix := 0; ix < 500; ix++ {
			// preserve the source's rounded multiply before its addition; Go otherwise fuses it on arm64.
			testX := -2.5 + roundedFloat64(float64(ix)*.008)
			for iy := 0; iy < 200; iy++ {
				testY := -1.5 + roundedFloat64(float64(iy)*.025)
				if inside(testX, testY) {
					count++
				}
			}
		}
		fmt.Printf("pnpoly: total=100000 inside=%d\npnpoly: DONE\n", count)
		return count == 29415
	case "triangl":
		from := [36]int{0, 0, 1, 1, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 12, 12, 13, 13, 14, 14}
		over := [36]int{1, 2, 3, 4, 4, 5, 1, 4, 6, 7, 7, 8, 2, 4, 8, 9, 3, 7, 4, 8, 4, 7, 5, 8, 6, 11, 7, 12, 7, 8, 11, 13, 8, 12, 9, 13}
		to := [36]int{3, 5, 6, 8, 7, 9, 0, 5, 10, 12, 11, 13, 0, 3, 12, 14, 1, 8, 2, 9, 1, 6, 2, 7, 3, 12, 4, 13, 3, 5, 10, 14, 4, 11, 5, 12}
		board := [15]bool{}
		for i := range board {
			board[i] = true
		}
		board[0] = false
		stack := [14]int{}
		depth, pegs, solutions := 0, 14, 0
		for depth >= 0 {
			if pegs == 1 {
				solutions++
				depth--
				if depth < 0 {
					break
				}
				last := stack[depth]
				board[from[last]], board[over[last]], board[to[last]] = true, true, false
				pegs++
				stack[depth] = last + 1
				continue
			}
			found := false
			for move := stack[depth]; move < 36; move++ {
				if board[from[move]] && board[over[move]] && !board[to[move]] {
					board[from[move]], board[over[move]], board[to[move]] = false, false, true
					pegs--
					stack[depth] = move
					depth++
					if depth < 14 {
						stack[depth] = 0
					}
					found = true
					break
				}
			}
			if !found {
				depth--
				if depth >= 0 {
					last := stack[depth]
					board[from[last]], board[over[last]], board[to[last]] = true, true, false
					pegs++
					stack[depth] = last + 1
				}
			}
		}
		ok := solutions == 29760
		fmt.Printf("triangl: solutions=%d\ntriangl: %s\n", solutions, pass(ok))
		return ok
	case "paraffins":
		result := 0
		for iteration := 0; iteration < 10; iteration++ {
			for size := 1; size <= 23; size++ {
				result = paraffinCount(size)
			}
		}
		ok := result == 5731580
		fmt.Printf("paraffins: nb(23) = %d\nparaffins: %s\n", result, pass(ok))
		return ok
	default:
		return false
	}
}

func fannkuch(n int) (int, int) {
	permutation, working, rotations := make([]int, n), make([]int, n), make([]int, n)
	for index := range permutation {
		permutation[index] = index
	}
	remaining, checksum, maximum, permutationCount := n, 0, 0, 0
	for {
		for remaining != 1 {
			rotations[remaining-1] = remaining
			remaining--
		}
		copy(working, permutation)
		flips := 0
		for prefix := working[0]; prefix != 0; prefix = working[0] {
			for left, right := 0, prefix; left < right; left, right = left+1, right-1 {
				working[left], working[right] = working[right], working[left]
			}
			flips++
		}
		if flips > maximum {
			maximum = flips
		}
		if permutationCount%2 == 0 {
			checksum += flips
		} else {
			checksum -= flips
		}
		permutationCount++
		remaining = 1
		for remaining < n {
			first := permutation[0]
			copy(permutation, permutation[1:remaining+1])
			permutation[remaining] = first
			rotations[remaining]--
			if rotations[remaining] > 0 {
				break
			}
			remaining++
		}
		if remaining == n {
			return checksum, maximum
		}
	}
}

func spectralElement(row, column int) float64 {
	sum := row + column
	return 1 / float64(sum*(sum+1)/2+row+1)
}

func spectralMultiply(input, output []float64, transpose bool) {
	for row := range output {
		total := 0.0
		for column, value := range input {
			if transpose {
				total += spectralElement(column, row) * value
			} else {
				total += spectralElement(row, column) * value
			}
		}
		output[row] = total
	}
}

func spectralMultiplyATA(input, output, temporary []float64) {
	spectralMultiply(input, temporary, false)
	spectralMultiply(temporary, output, true)
}

func benchmarkFile(parts ...string) (string, error) {
	directory, err := os.Getwd()
	if err != nil {
		return "", err
	}
	for {
		for _, root := range []string{directory, filepath.Join(directory, "test", "benchmark")} {
			path := filepath.Join(append([]string{root}, parts...)...)
			if _, err := os.Stat(path); err == nil {
				return path, nil
			}
		}
		parent := filepath.Dir(directory)
		if parent == directory {
			return "", os.ErrNotExist
		}
		directory = parent
	}
}

func readBenchmarkFile(parts ...string) ([]byte, error) {
	path, err := benchmarkFile(parts...)
	if err != nil {
		return nil, err
	}
	return os.ReadFile(path)
}

func awfyJSONFixture() ([]byte, error) {
	source, err := readBenchmarkFile("awfy", "json.ls")
	if err != nil {
		return nil, err
	}
	for _, line := range strings.Split(string(source), "\n") {
		if strings.HasPrefix(line, "let RAP = ") {
			fixture, err := strconv.Unquote(strings.TrimPrefix(line, "let RAP = "))
			return []byte(fixture), err
		}
	}
	return nil, os.ErrNotExist
}

func matchesBENGGolden(name, output string) bool {
	golden, err := readBenchmarkFile("beng", name+".txt")
	return err == nil && strings.TrimSpace(output) == strings.TrimSpace(string(golden))
}

func jetstreamAssignedString(filename, prefix string) (string, error) {
	source, err := readBenchmarkFile("jetstream", filename)
	if err != nil {
		return "", err
	}
	for _, line := range strings.Split(string(source), "\n") {
		if strings.HasPrefix(line, prefix) {
			return strconv.Unquote(strings.TrimSpace(strings.TrimPrefix(line, prefix)))
		}
	}
	return "", os.ErrNotExist
}

func jetstreamHexInteger(filename, name string) (*big.Int, error) {
	value, err := jetstreamAssignedString(filename, "let "+name+"  = ")
	if err != nil {
		value, err = jetstreamAssignedString(filename, "let "+name+" = ")
	}
	if err != nil {
		return nil, err
	}
	result, ok := new(big.Int).SetString(value, 16)
	if !ok {
		return nil, fmt.Errorf("invalid hexadecimal %s", name)
	}
	return result, nil
}

type fastaRecord struct {
	header   string
	sequence string
}

func parseFasta(input string) []fastaRecord {
	records := []fastaRecord{}
	var current *fastaRecord
	for _, line := range strings.Split(input, "\n") {
		line = strings.TrimSuffix(line, "\r")
		if strings.HasPrefix(line, ">") {
			records = append(records, fastaRecord{header: line[1:]})
			current = &records[len(records)-1]
		} else if current != nil && line != "" {
			current.sequence += line
		}
	}
	return records
}

func fastaRepeat(output *strings.Builder, identifier, description, source string, count int) {
	fmt.Fprintf(output, ">%s %s\n", identifier, description)
	position := 0
	for count > 0 {
		lineLength := minInt(count, 60)
		for index := 0; index < lineLength; index++ {
			output.WriteByte(source[position])
			position = (position + 1) % len(source)
		}
		output.WriteByte('\n')
		count -= lineLength
	}
}

func fastaRandom(output *strings.Builder, identifier, description, letters string, probabilities []float64, count int, seed *int) {
	fmt.Fprintf(output, ">%s %s\n", identifier, description)
	cumulative := make([]float64, len(probabilities))
	for index, probability := range probabilities {
		if index == 0 {
			cumulative[index] = probability
		} else {
			cumulative[index] = cumulative[index-1] + probability
		}
	}
	for count > 0 {
		lineLength := minInt(count, 60)
		for index := 0; index < lineLength; index++ {
			*seed = (*seed*3877 + 29573) % 139968
			value, choice := float64(*seed)/139968, 0
			for choice < len(letters)-1 && cumulative[choice] < value {
				choice++
			}
			output.WriteByte(letters[choice])
		}
		output.WriteByte('\n')
		count -= lineLength
	}
}

func nucleotideIndex(letter byte) int {
	switch letter {
	case 'A', 'a':
		return 0
	case 'C', 'c':
		return 1
	case 'G', 'g':
		return 2
	default:
		return 3
	}
}

type frequencyEntry struct {
	sequence string
	count    int
}

func nucleotideFrequencies(sequence string, width int) []frequencyEntry {
	counts := map[string]int{}
	for index := 0; index+width <= len(sequence); index++ {
		counts[sequence[index:index+width]]++
	}
	entries := make([]frequencyEntry, 0, len(counts))
	for key, count := range counts {
		entries = append(entries, frequencyEntry{key, count})
	}
	sort.Slice(entries, func(left, right int) bool {
		if entries[left].count != entries[right].count {
			return entries[left].count > entries[right].count
		}
		return entries[left].sequence < entries[right].sequence
	})
	return entries
}

func reverseComplement(sequence string) string {
	complements := map[byte]byte{'A': 'T', 'T': 'A', 'C': 'G', 'G': 'C', 'M': 'K', 'K': 'M', 'R': 'Y', 'Y': 'R', 'V': 'B', 'B': 'V', 'H': 'D', 'D': 'H'}
	result := make([]byte, len(sequence))
	for index := range sequence {
		letter := sequence[len(sequence)-1-index]
		if letter >= 'a' && letter <= 'z' {
			letter -= 'a' - 'A'
		}
		if complement, found := complements[letter]; found {
			result[index] = complement
		} else {
			result[index] = letter
		}
	}
	return string(result)
}

func runBENG(name string) bool {
	switch name {
	case "binarytrees":
		stretch := checkTree(makeTree(11))
		longLived := makeTree(10)
		ok := stretch == 4095
		fmt.Printf("stretch tree of depth 11\t check: %d\n", stretch)
		for depth := 4; depth <= 10; depth += 2 {
			iterations, total := 1<<(10-depth+4), 0
			for iteration := 0; iteration < iterations; iteration++ {
				total += checkTree(makeTree(depth))
			}
			ok = ok && total == iterations*((1<<(depth+1))-1)
			fmt.Printf("%d\t trees of depth %d\t check: %d\n", iterations, depth, total)
		}
		longCheck := checkTree(longLived)
		ok = ok && longCheck == 2047
		fmt.Printf("long lived tree of depth 10\t check: %d\n", longCheck)
		return ok
	case "fannkuch":
		checksum, maximum := fannkuch(7)
		ok := checksum == 228 && maximum == 16
		fmt.Printf("%d\nPfannkuchen(7) = %d\n", checksum, maximum)
		return ok
	case "mandelbrot":
		checksum, byteAccumulator, bits := 0, 0, 0
		for y := 0; y < 500; y++ {
			ci := 2*float64(y)/500 - 1
			for x := 0; x < 500; x++ {
				zr, zi, zrzr, zizi := 0.0, 0.0, 0.0, 0.0
				cr, escaped := 2*float64(x)/500-1.5, false
				for iteration := 0; iteration < 50 && !escaped; iteration++ {
					newZR := zrzr - zizi + cr
					zi = 2*zr*zi + ci
					zr = newZR
					zrzr, zizi = zr*zr, zi*zi
					escaped = zrzr+zizi > 4
				}
				if !escaped {
					byteAccumulator = byteAccumulator<<1 | 1
				} else {
					byteAccumulator <<= 1
				}
				bits++
				if bits == 8 {
					checksum ^= byteAccumulator
					byteAccumulator, bits = 0, 0
				} else if x == 499 {
					checksum ^= byteAccumulator << (8 - bits)
					byteAccumulator, bits = 0, 0
				}
			}
		}
		fmt.Printf("%d\n", checksum)
		return checksum == 255
	case "spectralnorm":
		u, v, temporary := make([]float64, 100), make([]float64, 100), make([]float64, 100)
		for index := range u {
			u[index] = 1
		}
		for iteration := 0; iteration < 10; iteration++ {
			spectralMultiplyATA(u, v, temporary)
			spectralMultiplyATA(v, u, temporary)
		}
		left, right := 0.0, 0.0
		for index := range u {
			left += u[index] * v[index]
			right += v[index] * v[index]
		}
		result := math.Sqrt(left / right)
		fmt.Printf("%.9f\n", result)
		return math.Abs(result-1.274219991) < .000000001
	case "nbody":
		system := makeNBodySystem()
		before := system.energy()
		for iteration := 0; iteration < 36000; iteration++ {
			system.advance()
		}
		after := system.energy()
		fmt.Printf("%.9f\n%.9f\n", before, after)
		return math.Abs(before-(-.169075164)) < .000000001 && math.Abs(after-(-.169014245)) < .000000001
	case "pidigits":
		q, r, s, t, k := big.NewInt(1), big.NewInt(0), big.NewInt(0), big.NewInt(1), big.NewInt(0)
		one, two, three, four, ten := big.NewInt(1), big.NewInt(2), big.NewInt(3), big.NewInt(4), big.NewInt(10)
		var output, digits strings.Builder
		for index := 0; index < 30; {
			k.Add(k, one)
			k2 := new(big.Int).Add(new(big.Int).Mul(k, two), one)
			newQ := new(big.Int).Mul(q, k)
			newR := new(big.Int).Mul(new(big.Int).Add(new(big.Int).Mul(two, q), r), k2)
			newS := new(big.Int).Mul(s, k)
			newT := new(big.Int).Mul(new(big.Int).Add(new(big.Int).Mul(two, s), t), k2)
			q, r, s, t = newQ, newR, newS, newT
			if q.Cmp(r) > 0 {
				continue
			}
			digit3 := new(big.Int).Quo(new(big.Int).Add(new(big.Int).Mul(three, q), r), new(big.Int).Add(new(big.Int).Mul(three, s), t))
			digit4 := new(big.Int).Quo(new(big.Int).Add(new(big.Int).Mul(four, q), r), new(big.Int).Add(new(big.Int).Mul(four, s), t))
			if digit3.Cmp(digit4) != 0 {
				continue
			}
			digits.WriteString(digit3.String())
			index++
			if index%10 == 0 {
				fmt.Fprintf(&output, "%s\t:%d\n", digits.String(), index)
				digits.Reset()
			}
			r = new(big.Int).Mul(new(big.Int).Sub(r, new(big.Int).Mul(digit3, t)), ten)
			q = new(big.Int).Mul(q, ten)
		}
		text := output.String()
		fmt.Print(text)
		return text == "3141592653\t:10\n5897932384\t:20\n6264338327\t:30\n"
	case "fasta":
		const alu = "GGCCGGGCGCGGTGGCTCACGCCTGTAATCCCAGCACTTTGGGAGGCCGAGGCGGGCGGATCACCTGAGGTCAGGAGTTCGAGACCAGCCTGGCCAACATGGTGAAACCCCGTCTCTACTAAAAATACAAAAATTAGCCGGGCGTGGTGGCGCGCGCCTGTAATCCCAGCTACTCGGGAGGCTGAGGCAGGAGAATCGCTTGAACCCGGGAGGCGGAGGTTGCAGTGAGCCGAGATCGCGCCACTGCACTCCAGCCTGGGCGACAGAGCGAGACTCCGTCTCAAAAA"
		var output strings.Builder
		seed := 42
		fastaRepeat(&output, "ONE", "Homo sapiens alu", alu, 2000)
		fastaRandom(&output, "TWO", "IUB ambiguity codes", "acgtBDHKMNRSVWY", []float64{.27, .12, .12, .27, .02, .02, .02, .02, .02, .02, .02, .02, .02, .02, .02}, 3000, &seed)
		fastaRandom(&output, "THREE", "Homo sapiens frequency", "acgt", []float64{.3029549426680, .1979883004921, .1975473066391, .3015094502008}, 5000, &seed)
		text := output.String()
		fmt.Print(text)
		return matchesBENGGolden("fasta", text)
	case "knucleotide":
		input, err := readBenchmarkFile("beng", "input", "fasta_1000.txt")
		if err != nil {
			return false
		}
		records := parseFasta(string(input))
		sequence := strings.ToUpper(records[len(records)-1].sequence)
		var output strings.Builder
		for _, width := range []int{1, 2} {
			for _, entry := range nucleotideFrequencies(sequence, width) {
				fmt.Fprintf(&output, "%s %.3f\n", entry.sequence, 100*float64(entry.count)/float64(len(sequence)-width+1))
			}
			output.WriteByte('\n')
		}
		for _, needle := range []string{"GGT", "GGTA", "GGTATT", "GGTATTTTAATT", "GGTATTTTAATTTATAGT"} {
			count := 0
			for index := 0; index+len(needle) <= len(sequence); index++ {
				if sequence[index:index+len(needle)] == needle {
					count++
				}
			}
			fmt.Fprintf(&output, "%d\t%s\n", count, needle)
		}
		text := output.String()
		fmt.Print(text)
		return matchesBENGGolden("knucleotide", text)
	case "regexredux":
		input, err := readBenchmarkFile("beng", "input", "fasta_1000.txt")
		if err != nil {
			return false
		}
		raw := string(input)
		records := parseFasta(raw)
		var bare strings.Builder
		for _, record := range records {
			bare.WriteString(record.sequence)
		}
		sequence := strings.ToLower(bare.String())
		patterns := []string{"agggtaaa|tttaccct", "[cgt]gggtaaa|tttaccc[acg]", "a[act]ggtaaa|tttacc[agt]t", "ag[act]gtaaa|tttac[agt]ct", "agg[act]taaa|ttta[agt]cct", "aggg[acg]aaa|ttt[cgt]ccct", "agggt[cgt]aa|tt[acg]taccct", "agggta[cgt]a|t[acg]ataccct", "agggtaa[cgt]|[acg]aataccct"}
		var output strings.Builder
		for _, pattern := range patterns {
			fmt.Fprintf(&output, "%s %d\n", pattern, len(regexp.MustCompile(pattern).FindAllString(sequence, -1)))
		}
		expanded := strings.NewReplacer("B", "(c|g|t)", "D", "(a|g|t)", "H", "(a|c|t)", "K", "(g|t)", "M", "(a|c)", "N", "(a|c|g|t)", "R", "(a|g)", "S", "(c|g)", "V", "(a|c|g)", "W", "(a|t)", "Y", "(c|t)").Replace(bare.String())
		fmt.Fprintf(&output, "\n%d\n%d\n%d\n", len(raw), len(sequence), len(expanded))
		text := output.String()
		fmt.Print(text)
		return matchesBENGGolden("regexredux", text)
	case "revcomp":
		input, err := readBenchmarkFile("beng", "input", "fasta_1000.txt")
		if err != nil {
			return false
		}
		var output strings.Builder
		for _, record := range parseFasta(string(input)) {
			fmt.Fprintf(&output, ">%s\n", record.header)
			sequence := reverseComplement(record.sequence)
			for index := 0; index < len(sequence); index += 60 {
				end := minInt(index+60, len(sequence))
				fmt.Fprintf(&output, "%s\n", sequence[index:end])
			}
		}
		text := output.String()
		fmt.Print(text)
		return matchesBENGGolden("revcomp", text)
	default:
		return false
	}
}

func jetstreamBase64() bool {
	bytes := make([]byte, 8192)
	seed := 12345
	for index := range bytes {
		hi, lo := seed/127773, seed%127773
		seed = 16807*lo - 2836*hi
		if seed <= 0 {
			seed += 2147483647
		}
		bytes[index] = byte(seed%26 + 'a')
	}
	for len(bytes) <= 16384 {
		encoded := base64.StdEncoding.EncodeToString(bytes)
		decoded, err := base64.StdEncoding.DecodeString(encoded)
		if err != nil || len(decoded) != len(bytes) {
			return false
		}
		for index := range bytes {
			if bytes[index] != decoded[index] {
				return false
			}
		}
		bytes = append(bytes, bytes...)
	}
	return true
}

func jetstreamBigDenary() bool {
	first, ok := new(big.Rat).SetString("8965168485622506189945604.1235068121348084163185216")
	if !ok {
		return false
	}
	second, ok := new(big.Rat).SetString("2480986213549488579706531.6546845013548451265890628")
	if !ok {
		return false
	}
	add := func() *big.Rat { return new(big.Rat).Add(first, second) }
	subtract := func() *big.Rat { return new(big.Rat).Sub(first, second) }
	negate := func() *big.Rat { return new(big.Rat).Neg(first) }
	multiply := func() *big.Rat { return new(big.Rat).Mul(first, second) }
	divide := func() *big.Rat { return new(big.Rat).Quo(first, second) }
	for _, operation := range []func() *big.Rat{add, subtract, negate, multiply, divide} {
		expected := operation()
		for iteration := 0; iteration < 10000; iteration++ {
			if expected.Cmp(operation()) != 0 {
				return false
			}
		}
	}
	return first.Cmp(second) > 0
}

func jetstreamAES() bool {
	plainText, err := jetstreamAssignedString("crypto_aes.ls", "    var plainText = ")
	if err != nil {
		return false
	}
	key := sha256.Sum256([]byte("O Romeo, Romeo! wherefore art thou Romeo?"))
	block, err := aes.NewCipher(key[:])
	if err != nil {
		return false
	}
	counter := [aes.BlockSize]byte{0x68, 0x23, 0x2b, 0x75, 0x34, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
	for iteration := 0; iteration < 40; iteration++ {
		cipherText, decrypted := make([]byte, len(plainText)), make([]byte, len(plainText))
		cipher.NewCTR(block, counter[:]).XORKeyStream(cipherText, []byte(plainText))
		cipher.NewCTR(block, counter[:]).XORKeyStream(decrypted, cipherText)
		if string(decrypted) != plainText {
			return false
		}
	}
	return true
}

func jetstreamMD5() bool {
	plainText, err := jetstreamAssignedString("crypto_md5.ls", "    var plain_text = ")
	if err != nil {
		return false
	}
	for iteration := 0; iteration < 4; iteration++ {
		plainText += plainText
	}
	for iteration := 0; iteration < 22; iteration++ {
		digest := md5.Sum([]byte(plainText))
		if fmt.Sprintf("%x", digest) != "a831e91e0f70eddcb70dc61c6f82f6cd" {
			return false
		}
	}
	return true
}

func jetstreamSHA1() bool {
	plainText, err := jetstreamAssignedString("crypto_sha1.ls", "    var plain_text = ")
	if err != nil {
		return false
	}
	for iteration := 0; iteration < 4; iteration++ {
		plainText += plainText
	}
	for iteration := 0; iteration < 25; iteration++ {
		digest := sha1.Sum([]byte(plainText))
		if fmt.Sprintf("%x", digest) != "2524d264def74cce2498bf112bedf00e6c0b796d" {
			return false
		}
	}
	return true
}

func jetstreamRSA() bool {
	n, err := jetstreamHexInteger("crypto_rsa.ls", "nValue")
	if err != nil {
		return false
	}
	d, err := jetstreamHexInteger("crypto_rsa.ls", "dValue")
	if err != nil {
		return false
	}
	p, err := jetstreamHexInteger("crypto_rsa.ls", "pValue")
	if err != nil {
		return false
	}
	q, err := jetstreamHexInteger("crypto_rsa.ls", "qValue")
	if err != nil {
		return false
	}
	plainText, err := jetstreamAssignedString("crypto_rsa.ls", "let TEXT = ")
	if err != nil {
		return false
	}
	key := &rsa.PrivateKey{PublicKey: rsa.PublicKey{N: n, E: 65537}, D: d, Primes: []*big.Int{p, q}}
	key.Precompute()
	if err := key.Validate(); err != nil {
		return false
	}
	for iteration := 0; iteration < 20; iteration++ {
		cipherText, err := rsa.EncryptPKCS1v15(cryptorand.Reader, &key.PublicKey, []byte(plainText))
		if err != nil {
			return false
		}
		decrypted, err := rsa.DecryptPKCS1v15(cryptorand.Reader, key, cipherText)
		if err != nil || string(decrypted) != plainText {
			return false
		}
	}
	return true
}

func jetstreamHashMap() bool {
	const count = 90000
	values := make(map[int]int, count)
	for key := 0; key < count; key++ {
		values[key] = 42
	}
	total := 0
	for repeat := 0; repeat < 5; repeat++ {
		for key := 0; key < count; key++ {
			total += values[key]
		}
	}
	keyTotal, valueTotal := 0, 0
	for key, value := range values {
		keyTotal += key
		valueTotal += value
	}
	return total == 42*count*5 && keyTotal == count*(count-1)/2 && valueTotal == 42*count
}

func jetstreamNBody() bool {
	for iteration := 0; iteration < 8; iteration++ {
		result := 0.0
		for steps := 300; steps <= 2400; steps *= 2 {
			system := makeNBodySystem()
			result += system.energy()
			for step := 0; step < steps; step++ {
				system.advance()
			}
			result += system.energy()
		}
		if int(math.Floor(-result*10000000)) != 13524862 {
			return false
		}
	}
	return true
}

func jetstreamRegexDNA() bool {
	input, err := readBenchmarkFile("jetstream", "regex_dna_data.json")
	if err != nil {
		return false
	}
	var fixture struct {
		Raw      string `json:"dna_raw"`
		Output   string `json:"expected_output"`
		Expanded string `json:"expected_dna"`
	}
	if json.Unmarshal(input, &fixture) != nil {
		return false
	}
	patterns := []string{"agggtaaa|tttaccct", "[cgt]gggtaaa|tttaccc[acg]", "a[act]ggtaaa|tttacc[agt]t", "ag[act]gtaaa|tttac[agt]ct", "agg[act]taaa|ttta[agt]cct", "aggg[acg]aaa|ttt[cgt]ccct", "agggt[cgt]aa|tt[acg]accct", "agggta[cgt]a|t[acg]taccct", "agggtaa[cgt]|[acg]ttaccct"}
	for iteration := 0; iteration < 2; iteration++ {
		dna := regexp.MustCompile(`(?m)^>.*\n|\n`).ReplaceAllString(fixture.Raw, "")
		var output strings.Builder
		for _, pattern := range patterns {
			fmt.Fprintf(&output, "%s %d\n", pattern, len(regexp.MustCompile("(?i)"+pattern).FindAllString(dna, -1)))
		}
		if output.String() != fixture.Output {
			return false
		}
		for _, replacement := range [][2]string{{"B", "(c|g|t)"}, {"D", "(a|g|t)"}, {"H", "(a|c|t)"}, {"K", "(g|t)"}, {"M", "(a|c)"}, {"N", "(a|c|g|t)"}, {"R", "(a|g)"}, {"S", "(c|t)"}, {"V", "(a|c|g)"}, {"W", "(a|t)"}, {"Y", "(c|t)"}} {
			dna = strings.Replace(dna, replacement[0], replacement[1], 1)
		}
		if dna != fixture.Expanded {
			return false
		}
	}
	return true
}

type splayPayload struct {
	left, right *splayPayload
	values      [10]int
	tag         float64
}

type splayNode struct {
	key         float64
	left, right *splayNode
	payload     *splayPayload
}

func nextSplayRandom(seed *int) float64 {
	hi, lo := *seed/127773, *seed%127773
	*seed = 16807*lo - 2836*hi
	if *seed <= 0 {
		*seed += 2147483647
	}
	return float64(*seed) / 2147483647
}

func splay(root *splayNode, key float64) *splayNode {
	if root == nil {
		return nil
	}
	dummy := &splayNode{}
	left, right, current := dummy, dummy, root
	for {
		if key < current.key {
			if current.left == nil {
				break
			}
			if key < current.left.key {
				top := current.left
				current.left, top.right, current = top.right, current, top
				if current.left == nil {
					break
				}
			}
			right.left, right, current = current, current, current.left
		} else if key > current.key {
			if current.right == nil {
				break
			}
			if key > current.right.key {
				top := current.right
				current.right, top.left, current = top.left, current, top
				if current.right == nil {
					break
				}
			}
			left.right, left, current = current, current, current.right
		} else {
			break
		}
	}
	left.right, right.left = current.left, current.right
	current.left, current.right = dummy.right, dummy.left
	return current
}

func splayInsert(root *splayNode, key float64, payload *splayPayload) *splayNode {
	if root == nil {
		return &splayNode{key: key, payload: payload}
	}
	root = splay(root, key)
	if root.key == key {
		return root
	}
	node := &splayNode{key: key, payload: payload}
	if key > root.key {
		node.left, node.right, root.right = root, root.right, nil
	} else {
		node.right, node.left, root.left = root, root.left, nil
	}
	return node
}

func splayRemove(root *splayNode, key float64) (*splayNode, *splayNode) {
	if root == nil {
		return nil, nil
	}
	root = splay(root, key)
	if root.key != key {
		return root, nil
	}
	removed := root
	if root.left == nil {
		return root.right, removed
	}
	right := root.right
	root = splay(root.left, key)
	root.right = right
	return root, removed
}

func splayFind(root *splayNode, key float64) (*splayNode, *splayNode) {
	if root == nil {
		return nil, nil
	}
	root = splay(root, key)
	if root.key == key {
		return root, root
	}
	return root, nil
}

func splayGreatestLessThan(root *splayNode, key float64) (*splayNode, *splayNode) {
	if root == nil {
		return nil, nil
	}
	root = splay(root, key)
	if root.key < key {
		return root, root
	}
	current := root.left
	for current != nil && current.right != nil {
		current = current.right
	}
	return root, current
}

func splayPayloadTree(depth int, tag float64) *splayPayload {
	if depth == 0 {
		payload := &splayPayload{tag: tag}
		for index := range payload.values {
			payload.values[index] = index
		}
		return payload
	}
	return &splayPayload{left: splayPayloadTree(depth-1, tag), right: splayPayloadTree(depth-1, tag)}
}

func splayInsertRandom(root *splayNode, seed *int) (*splayNode, float64) {
	key := nextSplayRandom(seed)
	root, found := splayFind(root, key)
	for found != nil {
		key = nextSplayRandom(seed)
		root, found = splayFind(root, key)
	}
	return splayInsert(root, key, splayPayloadTree(5, key)), key
}

func jetstreamSplay() bool {
	var root *splayNode
	seed := 49734321
	for index := 0; index < 8000; index++ {
		root, _ = splayInsertRandom(root, &seed)
	}
	for iteration := 0; iteration < 50; iteration++ {
		for modification := 0; modification < 80; modification++ {
			var key float64
			root, key = splayInsertRandom(root, &seed)
			var greatest *splayNode
			root, greatest = splayGreatestLessThan(root, key)
			if greatest == nil {
				root, _ = splayRemove(root, key)
			} else {
				root, _ = splayRemove(root, greatest.key)
			}
		}
	}
	count := 0
	stack := []*splayNode{root}
	for len(stack) != 0 {
		last := len(stack) - 1
		node := stack[last]
		stack = stack[:last]
		if node != nil {
			count++
			stack = append(stack, node.left, node.right)
		}
	}
	return count == 8000
}

const (
	navierWidth   = 128
	navierHeight  = 128
	navierRowSize = navierWidth + 2
	navierSize    = navierRowSize * (navierHeight + 2)
)

func navierAddFields(destination, source []float64, delta float64) {
	for index := range destination {
		destination[index] += delta * source[index]
	}
}

func navierSetBoundary(kind int, values []float64) {
	if kind == 1 {
		for column := 1; column <= navierWidth; column++ {
			values[column] = values[column+navierRowSize]
			values[column+(navierHeight+1)*navierRowSize] = values[column+navierHeight*navierRowSize]
		}
		for row := 1; row <= navierHeight; row++ {
			values[row*navierRowSize] = -values[1+row*navierRowSize]
			values[navierWidth+1+row*navierRowSize] = -values[navierWidth+row*navierRowSize]
		}
	} else if kind == 2 {
		for column := 1; column <= navierWidth; column++ {
			values[column] = -values[column+navierRowSize]
			values[column+(navierHeight+1)*navierRowSize] = -values[column+navierHeight*navierRowSize]
		}
		for row := 1; row <= navierHeight; row++ {
			values[row*navierRowSize] = values[1+row*navierRowSize]
			values[navierWidth+1+row*navierRowSize] = values[navierWidth+row*navierRowSize]
		}
	} else {
		for column := 1; column <= navierWidth; column++ {
			values[column] = values[column+navierRowSize]
			values[column+(navierHeight+1)*navierRowSize] = values[column+navierHeight*navierRowSize]
		}
		for row := 1; row <= navierHeight; row++ {
			values[row*navierRowSize] = values[1+row*navierRowSize]
			values[navierWidth+1+row*navierRowSize] = values[navierWidth+row*navierRowSize]
		}
	}
	edge := (navierHeight + 1) * navierRowSize
	values[0] = .5 * (values[1] + values[navierRowSize])
	values[edge] = .5 * (values[1+edge] + values[navierHeight*navierRowSize])
	values[navierWidth+1] = .5 * (values[navierWidth] + values[navierWidth+1+navierRowSize])
	values[navierWidth+1+edge] = .5 * (values[navierWidth+edge] + values[navierWidth+1+navierHeight*navierRowSize])
}

func navierLinearSolve(kind int, values, source []float64, coefficient, divisor float64, iterations int) {
	if coefficient == 0 && divisor == 1 {
		for row := 1; row <= navierHeight; row++ {
			for column := 1; column <= navierWidth; column++ {
				index := row*navierRowSize + column
				values[index] = source[index]
			}
		}
		navierSetBoundary(kind, values)
		return
	}
	inverse := 1 / divisor
	for iteration := 0; iteration < iterations; iteration++ {
		for row := 1; row <= navierHeight; row++ {
			previous, current, next := (row-1)*navierRowSize, row*navierRowSize+1, (row+1)*navierRowSize
			last := values[current-1]
			for column := 1; column <= navierWidth; column++ {
				last = (source[current] + coefficient*(last+values[current+1]+values[previous+1]+values[next+1])) * inverse
				values[current] = last
				current, previous, next = current+1, previous+1, next+1
			}
		}
		navierSetBoundary(kind, values)
	}
}

func navierAdvect(kind int, destination, source, horizontal, vertical []float64, delta float64) {
	widthDelta, heightDelta := delta*navierWidth, delta*navierHeight
	for row := 1; row <= navierHeight; row++ {
		for column := 1; column <= navierWidth; column++ {
			index := row*navierRowSize + column
			x, y := float64(column)-widthDelta*horizontal[index], float64(row)-heightDelta*vertical[index]
			x = math.Max(.5, math.Min(float64(navierWidth)+.5, x))
			y = math.Max(.5, math.Min(float64(navierHeight)+.5, y))
			x0, y0 := int(x), int(y)
			x1, y1 := x0+1, y0+1
			sx1, sy1 := x-float64(x0), y-float64(y0)
			sx0, sy0 := 1-sx1, 1-sy1
			row0, row1 := y0*navierRowSize, y1*navierRowSize
			destination[index] = sx0*(sy0*source[x0+row0]+sy1*source[x0+row1]) + sx1*(sy0*source[x1+row0]+sy1*source[x1+row1])
		}
	}
	navierSetBoundary(kind, destination)
}

func navierProject(horizontal, vertical, pressure, divergence []float64, iterations int) {
	factor := -.5 / math.Sqrt(float64(navierWidth*navierHeight))
	for row := 1; row <= navierHeight; row++ {
		for column := 1; column <= navierWidth; column++ {
			index := row*navierRowSize + column
			divergence[index] = factor * (horizontal[index+1] - horizontal[index-1] + vertical[index+navierRowSize] - vertical[index-navierRowSize])
			pressure[index] = 0
		}
	}
	navierSetBoundary(0, divergence)
	navierSetBoundary(0, pressure)
	navierLinearSolve(0, pressure, divergence, 1, 4, iterations)
	for row := 1; row <= navierHeight; row++ {
		for column := 1; column <= navierWidth; column++ {
			index := row*navierRowSize + column
			horizontal[index] -= .5 * navierWidth * (pressure[index+1] - pressure[index-1])
			vertical[index] -= .5 * navierHeight * (pressure[index+navierRowSize] - pressure[index-navierRowSize])
		}
	}
	navierSetBoundary(1, horizontal)
	navierSetBoundary(2, vertical)
}

func navierVelocityStep(horizontal, vertical, priorHorizontal, priorVertical []float64, delta float64, iterations int) {
	navierAddFields(horizontal, priorHorizontal, delta)
	navierAddFields(vertical, priorVertical, delta)
	for index := range horizontal {
		horizontal[index], priorHorizontal[index] = priorHorizontal[index], horizontal[index]
		vertical[index], priorVertical[index] = priorVertical[index], vertical[index]
	}
	navierLinearSolve(1, horizontal, priorHorizontal, 0, 1, iterations)
	navierLinearSolve(2, vertical, priorVertical, 0, 1, iterations)
	navierProject(horizontal, vertical, priorHorizontal, priorVertical, iterations)
	for index := range horizontal {
		horizontal[index], priorHorizontal[index] = priorHorizontal[index], horizontal[index]
		vertical[index], priorVertical[index] = priorVertical[index], vertical[index]
	}
	navierAdvect(1, horizontal, priorHorizontal, priorHorizontal, priorVertical, delta)
	navierAdvect(2, vertical, priorVertical, priorHorizontal, priorVertical, delta)
	navierProject(horizontal, vertical, priorHorizontal, priorVertical, iterations)
}

func navierDensityStep(density, priorDensity, horizontal, vertical []float64, delta float64, iterations int) {
	navierAddFields(density, priorDensity, delta)
	navierLinearSolve(0, priorDensity, density, 0, 1, iterations)
	navierAdvect(0, density, priorDensity, horizontal, vertical, delta)
}

func navierAddPoints(density, horizontal, vertical []float64) {
	for index := 1; index <= 64; index++ {
		value := 64.0
		first := index + 1 + (index+1)*navierRowSize
		horizontal[first], vertical[first], density[first] = value, value, 5
		second := index + 1 + (64-index+1)*navierRowSize
		horizontal[second], vertical[second], density[second] = -value, -value, 20
		third := 128 - index + 1 + (64+index+1)*navierRowSize
		horizontal[third], vertical[third], density[third] = -value, -value, 30
	}
}

func jetstreamNavierStokes() int {
	density, priorDensity := make([]float64, navierSize), make([]float64, navierSize)
	horizontal, priorHorizontal := make([]float64, navierSize), make([]float64, navierSize)
	vertical, priorVertical := make([]float64, navierSize), make([]float64, navierSize)
	framesTillAdd, framesBetween := 0, 5
	for frame := 0; frame < 15; frame++ {
		for index := range density {
			priorDensity[index], priorHorizontal[index], priorVertical[index] = 0, 0, 0
		}
		if framesTillAdd == 0 {
			navierAddPoints(priorDensity, priorHorizontal, priorVertical)
			framesTillAdd, framesBetween = framesBetween, framesBetween+1
		} else {
			framesTillAdd--
		}
		navierVelocityStep(horizontal, vertical, priorHorizontal, priorVertical, .1, 20)
		navierDensityStep(density, priorDensity, horizontal, vertical, .1, 20)
	}
	result := 0
	for index := 7000; index < 7100; index++ {
		result += int(density[index] * 10)
	}
	return result
}

type matrix4 [16]float64

func cubeIdentity() matrix4 { return matrix4{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1} }

func cubeMultiply(left, right matrix4) matrix4 {
	var result matrix4
	for row := 0; row < 4; row++ {
		for column := 0; column < 4; column++ {
			for index := 0; index < 4; index++ {
				result[row*4+column] += left[row*4+index] * right[index*4+column]
			}
		}
	}
	return result
}

func cubeTransform(matrix matrix4, vector [4]float64) [4]float64 {
	var result [4]float64
	for row := 0; row < 4; row++ {
		result[row] = matrix[row*4]*vector[0] + matrix[row*4+1]*vector[1] + matrix[row*4+2]*vector[2] + matrix[row*4+3]*vector[3]
	}
	return result
}

func cubeTranslate(matrix matrix4, x, y, z float64) matrix4 {
	return cubeMultiply(matrix4{1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z, 0, 0, 0, 1}, matrix)
}

func cubeRotate(matrix matrix4, x, y, z float64) matrix4 {
	toRadians := math.Pi / 180
	cx, sx := math.Cos(x*toRadians), math.Sin(x*toRadians)
	cy, sy := math.Cos(y*toRadians), math.Sin(y*toRadians)
	cz, sz := math.Cos(z*toRadians), math.Sin(z*toRadians)
	matrix = cubeMultiply(matrix4{1, 0, 0, 0, 0, cx, -sx, 0, 0, sx, cx, 0, 0, 0, 0, 1}, matrix)
	matrix = cubeMultiply(matrix4{cy, 0, sy, 0, 0, 1, 0, 0, -sy, 0, cy, 0, 0, 0, 0, 1}, matrix)
	return cubeMultiply(matrix4{cz, -sz, 0, 0, sz, cz, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, matrix)
}

func cubeNormal(first, second, third [3]float64) [3]float64 {
	left := [3]float64{first[0] - second[0], first[1] - second[1], first[2] - second[2]}
	right := [3]float64{third[0] - second[0], third[1] - second[1], third[2] - second[2]}
	result := [3]float64{left[1]*right[2] - left[2]*right[1], left[2]*right[0] - left[0]*right[2], left[0]*right[1] - left[1]*right[0]}
	length := math.Sqrt(result[0]*result[0] + result[1]*result[1] + result[2]*result[2])
	return [3]float64{result[0] / length, result[1] / length, result[2] / length}
}

func cubeDrawLine(first, second [2]float64, previous int) int {
	return int(math.Round(float64(previous) + math.Max(math.Abs(second[0]-first[0]), math.Abs(second[1]-first[1]))))
}

func jetstreamCube(size int) float64 {
	length := float64(size)
	points := [9][4]float64{{-length, -length, length, 1}, {-length, length, length, 1}, {length, length, length, 1}, {length, -length, length, 1}, {-length, -length, -length, 1}, {-length, length, -length, 1}, {length, length, -length, 1}, {length, -length, -length, 1}, {0, 0, 0, 1}}
	faces := [6][4]int{{0, 1, 2, 3}, {3, 2, 6, 7}, {4, 5, 6, 7}, {4, 5, 1, 0}, {4, 0, 3, 7}, {1, 5, 6, 2}}
	var normals [6][3]float64
	for index, face := range faces {
		normals[index] = cubeNormal([3]float64{points[face[0]][0], points[face[0]][1], points[face[0]][2]}, [3]float64{points[face[1]][0], points[face[1]][1], points[face[1]][2]}, [3]float64{points[face[2]][0], points[face[2]][1], points[face[2]][2]})
	}
	matrix := cubeTranslate(cubeIdentity(), 150, 150, 20)
	for index := range points {
		points[index] = cubeTransform(matrix, points[index])
	}
	for iteration := 0; iteration < 51; iteration++ {
		center := points[8]
		transform := cubeTranslate(cubeIdentity(), -center[0], -center[1], -center[2])
		transform = cubeRotate(transform, 1, 3, 5)
		transform = cubeTranslate(transform, center[0], center[1], center[2])
		matrix = cubeMultiply(transform, matrix)
		for index := len(points) - 1; index >= 0; index-- {
			points[index] = cubeTransform(transform, points[index])
		}
		lastPixel := 0
		for faceIndex, face := range faces {
			transformed := cubeTransform(matrix, [4]float64{normals[faceIndex][0], normals[faceIndex][1], normals[faceIndex][2], 0})
			if transformed[2] < 0 {
				for index := 0; index < 4; index++ {
					first, second := points[face[index]], points[face[(index+1)%4]]
					lastPixel = cubeDrawLine([2]float64{first[0], first[1]}, [2]float64{second[0], second[1]}, lastPixel)
				}
			}
		}
	}
	total := 0.0
	for _, point := range points {
		total += point[0] + point[1] + point[2] + point[3]
	}
	return total
}

func jetstreamCube3D() bool {
	for iteration := 0; iteration < 8; iteration++ {
		for size := 20; size <= 160; size *= 2 {
			if math.Abs(jetstreamCube(size)-2889) > .001 {
				return false
			}
		}
	}
	return true
}

type rayVector [3]float64

func rayAdd(left, right rayVector) rayVector {
	return rayVector{left[0] + right[0], left[1] + right[1], left[2] + right[2]}
}
func raySub(left, right rayVector) rayVector {
	return rayVector{left[0] - right[0], left[1] - right[1], left[2] - right[2]}
}
func rayScale(vector rayVector, scale float64) rayVector {
	return rayVector{vector[0] * scale, vector[1] * scale, vector[2] * scale}
}
func rayDot(left, right rayVector) float64 {
	return left[0]*right[0] + left[1]*right[1] + left[2]*right[2]
}
func rayCross(left, right rayVector) rayVector {
	return rayVector{left[1]*right[2] - left[2]*right[1], left[2]*right[0] - left[0]*right[2], left[0]*right[1] - left[1]*right[0]}
}
func rayNormalize(vector rayVector) rayVector {
	return rayScale(vector, 1/math.Sqrt(rayDot(vector, vector)))
}

type rayTriangle struct{ a, b, c rayVector }

func (triangle rayTriangle) intersect(origin, direction rayVector, near, far float64) (float64, bool) {
	edge1, edge2 := raySub(triangle.b, triangle.a), raySub(triangle.c, triangle.a)
	p := rayCross(direction, edge2)
	determinant := rayDot(edge1, p)
	if determinant > -.0000001 && determinant < .0000001 {
		return 0, false
	}
	inverse := 1 / determinant
	t := raySub(origin, triangle.a)
	u := rayDot(t, p) * inverse
	if u < 0 || u > 1 {
		return 0, false
	}
	q := rayCross(t, edge1)
	v := rayDot(direction, q) * inverse
	if v < 0 || u+v > 1 {
		return 0, false
	}
	distance := rayDot(edge2, q) * inverse
	return distance, distance >= near && distance <= far
}

func rayCameraDirections(origin, lookAt, up rayVector) (rayVector, rayVector, rayVector, rayVector) {
	zAxis := rayNormalize(raySub(lookAt, origin))
	xAxis := rayNormalize(rayCross(up, zAxis))
	yAxis := rayNormalize(rayCross(xAxis, rayScale(zAxis, -1)))
	transform := func(vector rayVector) rayVector {
		return rayVector{rayDot(xAxis, vector), rayDot(yAxis, vector), rayDot(zAxis, vector)}
	}
	return transform(rayNormalize(rayVector{-.7, .7, 1})), transform(rayNormalize(rayVector{.7, .7, 1})), transform(rayNormalize(rayVector{.7, -.7, 1})), transform(rayNormalize(rayVector{-.7, -.7, 1}))
}

func jetstreamRayTraceScene() int {
	tfl, tfr, tbl, tbr := rayVector{-10, 10, -10}, rayVector{10, 10, -10}, rayVector{-10, 10, 10}, rayVector{10, 10, 10}
	bfl, bfr, bbl, bbr := rayVector{-10, -10, -10}, rayVector{10, -10, -10}, rayVector{-10, -10, 10}, rayVector{10, -10, 10}
	triangles := []rayTriangle{{tfl, tfr, bfr}, {tfl, bfr, bfl}, {tbl, tbr, bbr}, {tbl, bbr, bbl}, {tbl, tfl, bbl}, {tfl, bfl, bbl}, {tbr, tfr, bbr}, {tfr, bfr, bbr}, {tbl, tbr, tfr}, {tbl, tfr, tfl}, {bbl, bbr, bfr}, {bbl, bfr, bfl}}
	ffl, ffr, fbl, fbr := rayVector{-1000, -30, -1000}, rayVector{1000, -30, -1000}, rayVector{-1000, -30, 1000}, rayVector{1000, -30, 1000}
	triangles = append(triangles, rayTriangle{fbl, fbr, ffr}, rayTriangle{fbl, ffr, ffl})
	origin := rayVector{-40, 40, 40}
	d0, d1, d2, d3 := rayCameraDirections(origin, rayVector{}, rayVector{0, 1, 0})
	pixels := 0
	for y := 0; y < 30; y++ {
		yFraction := float64(y) / 30
		left := rayAdd(rayScale(d0, yFraction), rayScale(d3, 1-yFraction))
		right := rayAdd(rayScale(d1, yFraction), rayScale(d2, 1-yFraction))
		for x := 0; x < 30; x++ {
			xFraction := float64(x) / 30
			direction := rayNormalize(rayAdd(rayScale(left, xFraction), rayScale(right, 1-xFraction)))
			closest := 1000000.0
			for _, triangle := range triangles {
				if distance, hit := triangle.intersect(origin, direction, .0001, closest); hit {
					closest = distance
				}
			}
			pixels++
		}
	}
	return pixels
}

func jetstreamRayTrace() bool {
	pixels := 0
	for iteration := 0; iteration < 8; iteration++ {
		pixels += jetstreamRayTraceScene()
	}
	return pixels == 7200
}

type dataflowVariable struct {
	value     int
	listeners []func()
}

func (variable *dataflowVariable) set(value int) {
	variable.value = value
	for _, listener := range variable.listeners {
		listener()
	}
}

func linkDataflow(variable *dataflowVariable, listener func()) {
	variable.listeners = append(variable.listeners, listener)
}

func dataflowChain(size int) bool {
	variables := make([]*dataflowVariable, size+1)
	for index := range variables {
		variables[index] = &dataflowVariable{}
	}
	for index := 0; index < size; index++ {
		from, to := variables[index], variables[index+1]
		linkDataflow(from, func() { to.set(from.value) })
	}
	for value := 0; value < 100; value++ {
		variables[0].set(value)
		if variables[size].value != value {
			return false
		}
	}
	return true
}

type dataflowScaleConstraint struct {
	source, scale, offset, destination *dataflowVariable
	updating                           bool
}

func (constraint *dataflowScaleConstraint) forward() {
	if constraint.updating {
		return
	}
	constraint.updating = true
	constraint.destination.set(constraint.source.value*constraint.scale.value + constraint.offset.value)
	constraint.updating = false
}

func (constraint *dataflowScaleConstraint) backward() {
	if constraint.updating {
		return
	}
	constraint.updating = true
	constraint.source.set((constraint.destination.value - constraint.offset.value) / constraint.scale.value)
	constraint.updating = false
}

func newDataflowScale(source, scale, offset, destination *dataflowVariable) *dataflowScaleConstraint {
	constraint := &dataflowScaleConstraint{source: source, scale: scale, offset: offset, destination: destination}
	linkDataflow(source, constraint.forward)
	linkDataflow(scale, constraint.forward)
	linkDataflow(offset, constraint.forward)
	linkDataflow(destination, constraint.backward)
	constraint.forward()
	return constraint
}

func dataflowProjection(size int) bool {
	scale, offset := &dataflowVariable{value: 10}, &dataflowVariable{value: 1000}
	sources, destinations := make([]*dataflowVariable, size), make([]*dataflowVariable, size)
	for index := 0; index < size; index++ {
		sources[index], destinations[index] = &dataflowVariable{value: index + 1}, &dataflowVariable{value: index + 1}
		newDataflowScale(sources[index], scale, offset, destinations[index])
	}
	last := size - 1
	sources[last].set(17)
	if destinations[last].value != 1170 {
		return false
	}
	destinations[last].set(1050)
	if sources[last].value != 5 {
		return false
	}
	scale.set(5)
	for index := 0; index < size-1; index++ {
		if destinations[index].value != (index+1)*5+1000 {
			return false
		}
	}
	offset.set(2000)
	for index := 0; index < size-1; index++ {
		if destinations[index].value != (index+1)*5+2000 {
			return false
		}
	}
	return true
}

func awfyDeltaBlue() bool {
	for iteration := 0; iteration < 20; iteration++ {
		if !dataflowChain(100) || !dataflowProjection(100) {
			return false
		}
	}
	return true
}

func jetstreamDeltaBlue() bool {
	for iteration := 0; iteration < 20; iteration++ {
		if !dataflowChain(100) || !dataflowProjection(100) {
			return false
		}
	}
	return true
}

func runJetStream(name string) bool {
	var ok bool
	switch name {
	case "base64":
		for iteration := 0; iteration < 8; iteration++ {
			ok = jetstreamBase64()
			if !ok {
				break
			}
		}
		fmt.Printf("base64: %s\n", pass(ok))
		return ok
	case "bigdenary":
		ok = jetstreamBigDenary()
		fmt.Printf("bigdenary: %s\n", pass(ok))
		return ok
	case "crypto_aes":
		ok = jetstreamAES()
		fmt.Printf("crypto-aes: %s\n", pass(ok))
		return ok
	case "crypto_md5":
		ok = jetstreamMD5()
		fmt.Printf("crypto-md5: %s\n", pass(ok))
		return ok
	case "crypto_rsa":
		ok = jetstreamRSA()
		fmt.Printf("crypto-rsa: %s\n", pass(ok))
		return ok
	case "crypto_sha1":
		ok = jetstreamSHA1()
		fmt.Printf("crypto-sha1: %s\n", pass(ok))
		return ok
	case "hashmap":
		ok = jetstreamHashMap()
		fmt.Printf("hash-map: %s\n", pass(ok))
		return ok
	case "nbody":
		ok = jetstreamNBody()
		fmt.Printf("nbody: %s\n", pass(ok))
		return ok
	case "regex_dna":
		ok = jetstreamRegexDNA()
		fmt.Printf("regex-dna: %s\n", pass(ok))
		return ok
	case "splay":
		ok = jetstreamSplay()
		fmt.Printf("splay: %s\n", pass(ok))
		return ok
	case "navier_stokes":
		result := jetstreamNavierStokes()
		ok = result == 77
		fmt.Printf("navier-stokes: %s (checksum=%d)\n", pass(ok), result)
		return ok
	case "cube3d":
		ok = jetstreamCube3D()
		fmt.Printf("3d-cube: %s\n", pass(ok))
		return ok
	case "raytrace3d":
		ok = jetstreamRayTrace()
		fmt.Printf("3d-raytrace: %s (pixels=7200)\n", pass(ok))
		return ok
	case "richards":
		ok = true
		for iteration := 0; iteration < 50; iteration++ {
			ok = ok && richardsOnce(1000, 2322, 928)
		}
		fmt.Printf("richards: %s\n", pass(ok))
		return ok
	case "deltablue":
		ok = jetstreamDeltaBlue()
		fmt.Printf("deltablue: %s\n", pass(ok))
		return ok
	default:
		return false
	}
}

func Run(suite, name string) bool {
	switch suite {
	case "r7rs":
		return runR7RS(name)
	case "awfy":
		return runAWFY(name)
	case "kostya":
		return runKostya(name)
	case "larceny":
		return runLarceny(name)
	case "beng":
		return runBENG(name)
	case "jetstream":
		return runJetStream(name)
	case "cow_document_edit":
		return runCOWDocumentEdit()
	default:
		return false
	}
}
