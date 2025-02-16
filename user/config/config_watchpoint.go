package config

import (
	"encoding/json"
	"fmt"
)

// 结合其他项目构想一种新的方案 便于后续增补各类结构体的数据解析
// 而不是依赖配置文件去转换 某种程度上来说 硬编码反而是更好的选择

const MAX_POINT_ARG_COUNT = 10
const READ_INDEX_SKIP uint32 = 100
const READ_INDEX_REG uint32 = 101

const (
	FORBIDDEN uint32 = iota
	SYS_ENTER_EXIT
	SYS_ENTER
	SYS_EXIT
	UPROBE_ENTER_READ
)

type ArgType struct {
	ReadIndex      uint32
	AliasType      uint32
	Type           uint32
	Size           uint32
	ItemPerSize    uint32
	ItemCountIndex uint32
	ReadOffset     uint32
}

type IWatchPoint interface {
	Name() string
	Format() string
	Clone() IWatchPoint
}

type PointArgs struct {
	PointName string
	Ret       PointArg
	Args      []PointArg
}

type PArgs = PointArgs

type FilterArgType struct {
	ReadFlag uint32
	ArgType
}

type PointArg struct {
	ArgName  string
	ReadFlag uint32
	ArgType
	ArgValue string
}

type PArg = PointArg

func (this *ArgType) SetType(item uint32) {
	this.Type = item
}

func (this *ArgType) SetSize(size uint32) {
	this.Size = size
}

func (this *ArgType) SetCountIndex(index uint32) {
	this.ItemCountIndex = index
}

func (this *ArgType) SetReadIndex(index uint32) {
	this.ReadIndex = index
}

func (this *ArgType) SetReadOffset(offset uint32) {
	this.ReadOffset = offset
}

func (this *ArgType) SetItemPerSize(persize uint32) {
	this.ItemPerSize = persize
}

func (this *ArgType) NewType(item uint32) ArgType {
	at := this.Clone()
	at.Type = item
	return at
}

func (this *ArgType) NewSize(size uint32) ArgType {
	at := this.Clone()
	at.Size = size
	return at
}

func (this *ArgType) NewCountIndex(index uint32) ArgType {
	at := this.Clone()
	at.ItemCountIndex = index
	return at
}

func (this *ArgType) NewReadIndex(index uint32) ArgType {
	at := this.Clone()
	at.ReadIndex = index
	return at
}

func (this *ArgType) NewReadOffset(offset uint32) ArgType {
	at := this.Clone()
	at.ReadOffset = offset
	return at
}

func (this *ArgType) NewItemPerSize(persize uint32) ArgType {
	at := this.Clone()
	at.ItemPerSize = persize
	return at
}

func (this *ArgType) String() string {
	var s string = ""
	s += fmt.Sprintf("read_index:%d, alias_type:%d type:%d ", this.ReadIndex, this.AliasType, this.Type)
	s += fmt.Sprintf("size:%d per:%d count_index:%d ", this.Size, this.ItemPerSize, this.ItemCountIndex)
	s += fmt.Sprintf("off:%d", this.ReadOffset)
	return s
}

func (this *ArgType) Clone() ArgType {
	// 在涉及到类型变更的时候 记得先调用这个
	at := ArgType{}
	at.ReadIndex = this.ReadIndex
	at.AliasType = this.AliasType
	at.Type = this.Type
	at.Size = this.Size
	at.ItemPerSize = this.ItemPerSize
	at.ItemCountIndex = this.ItemCountIndex
	at.ReadIndex = this.ReadIndex
	return at
}

func (this *PointArg) SetValue(value string) {
	this.ArgValue = value
}

func (this *PointArg) AppendValue(value string) {
	this.ArgValue += value
}

func AT(arg_alias_type, arg_type, read_count uint32) ArgType {
	return ArgType{READ_INDEX_REG, arg_alias_type, arg_type, read_count, 1, READ_INDEX_SKIP, 0}
}

func PA(nr string, args []PArg) PArgs {
	return PArgs{nr, B("ret", UINT64), args}
}

func (this *PointArgs) Clone() IWatchPoint {
	args := new(PointArgs)
	args.PointName = this.PointName
	args.Ret = this.Ret
	args.Args = this.Args
	return args
}

func (this *PointArgs) Format() string {
	args, err := json.Marshal(this.Args)
	if err != nil {
		panic(fmt.Sprintf("Args Format err:%v", err))
	}
	return fmt.Sprintf("[%s] %d %s", this.PointName, len(this.Args), args)
}

func (this *PointArgs) Name() string {
	return this.PointName
}

func NewWatchPoint(name string) IWatchPoint {
	point := &PointArgs{}
	point.PointName = name
	return point
}

func NewSysCallWatchPoint(name string) IWatchPoint {
	point := &SysCallArgs{}
	return point
}

func Register(p IWatchPoint) {
	if p == nil {
		panic("Register watchpoint is nil")
	}
	name := p.Name()
	if _, dup := watchpoints[name]; dup {
		panic(fmt.Sprintf("Register called twice for watchpoint %s", name))
	}
	watchpoints[name] = p
	// 给 syscall 单独维护一个 map 这样便于在解析的时候快速获取 point 配置
	nr_point, ok := (p).(*SysCallArgs)
	if ok {
		if _, dup := nrwatchpoints[nr_point.NR]; dup {
			panic(fmt.Sprintf("Register called twice for nrwatchpoints %s", name))
		}
		nrwatchpoints[nr_point.NR] = nr_point
	}
}

func GetAllWatchPoints() map[string]IWatchPoint {
	return watchpoints
}

func GetWatchPointByNR(nr uint32) IWatchPoint {
	m, f := nrwatchpoints[nr]
	if f {
		return m
	}
	return nil
}

func GetWatchPointByName(pointName string) IWatchPoint {
	m, f := watchpoints[pointName]
	if f {
		return m
	}
	return nil
}

var watchpoints = make(map[string]IWatchPoint)
var nrwatchpoints = make(map[uint32]IWatchPoint)
