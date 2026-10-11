import React, {useState} from 'react';
import {createRoot} from 'react-dom/client';
import {Button, ConfigProvider, Input, Select, Space, Flex, Row, Col, Progress, Timeline, Statistic, Badge, Descriptions, Skeleton, Card, Avatar, Empty, Result, Typography, Pagination, Segmented, Tag, Rate, Alert, Spin} from 'antd';
import {ClockCircleOutlined, UserOutlined, CheckCircleOutlined, PlusOutlined} from '@ant-design/icons';
import zhCN from 'antd/locale/zh_CN';

// independent reference scenarios use the same labels and state as native fixtures.
function Composition() {
    const [revision, setRevision] = useState(0);
    return <section data-case="composition">
        <Space direction="vertical">
            <Button id="parent-update" onClick={() => setRevision(revision + 1)}>Update parent</Button>
            <output id="revision">{revision}</output>
            <Input id="draft" defaultValue="Seed" aria-label="Draft" />
            <Select id="choice" defaultValue="a" style={{width: 180}}
                options={[{value: 'a', label: 'Alpha'}, {value: 'b', label: 'Beta'}]} />
        </Space>
    </section>;
}

function ButtonModes() {
    const [count,setCount] = useState(0);
    const [last,setLast] = useState('');
    const [pending,setPending] = useState(false);
    const [removed,setRemoved] = useState(false);
    const [submits,setSubmits] = useState(0);
    const icon = <PlusOutlined/>;
    const make = (id,title,props={}) => <Button id={id} {...props} onClick={()=>{setCount(n=>n+1);setLast(id);}}>{title}</Button>;
    return <section data-case="button-modes" style={{display:'flex',flexDirection:'column',alignItems:'flex-start',gap:12,fontSize:14,lineHeight:'22px',color:'rgba(0,0,0,0.88)'}}>
        <div style={{display:'flex',alignItems:'center',gap:12}}>
            {make('button-primary','Primary',{type:'primary'})}{make('button-default','Default')}
            {make('button-dashed','Dashed',{type:'dashed'})}{make('button-text','Text',{type:'text'})}
            {make('button-link','Link',{type:'link',href:'#button-anchor'})}{make('button-danger','Danger',{danger:true})}
        </div>
        <div style={{display:'flex',alignItems:'center',gap:12}}>
            {make('button-small','Small',{size:'small'})}{make('button-middle','Middle')}{make('button-large','Large',{size:'large'})}
            {make('button-circle',null,{shape:'circle',icon,'aria-label':'Add'})}{make('button-round','Round',{shape:'round'})}
            {make('button-icon',null,{icon,'aria-label':'Add only'})}{make('button-end','End',{icon,iconPlacement:'end'})}
        </div>
        <div style={{display:'flex',alignItems:'center',gap:12}}>
            {make('button-disabled','Disabled',{disabled:true})}{make('button-loading','Loading',{loading:true})}
            {make('button-custom','Custom',{loading:{icon:<b>L</b>}})}
            {make('button-parts','Parts',{icon:<i>I</i>,classNames:{root:'authored-button',icon:'authored-image',content:'authored-caption'},styles:{content:{fontWeight:700}}})}
            {make('button-disabled-link','Disabled link',{disabled:true,href:'#button-anchor'})}
        </div>
        <div style={{display:'flex',alignItems:'center',gap:12}}>
            {make('button-delay','Delayed',{loading:{delay:5000}})}
            <Button id="button-toggle" onClick={()=>setPending(value=>!value)}>Toggle</Button>
            <Button id="button-remove" onClick={()=>setRemoved(true)}>Remove</Button>
        </div>
        {make('button-controlled','Controlled',{loading:pending?{delay:300}:false})}
        {!removed && make('button-removable','Removable',{loading:{delay:60000}})}
        <form style={{display:'flex',alignItems:'center'}} onSubmit={evt=>{evt.preventDefault();setSubmits(n=>n+1);}}><input id="button-form-input" defaultValue="seed" style={{font:'inherit',boxSizing:'border-box',width:180,height:32,border:'1px solid #d9d9d9',borderRadius:6,padding:'4px 11px'}}/>
            {make('button-submit','Submit',{htmlType:'submit'})}{make('button-reset','Reset',{htmlType:'reset'})}
        </form>
        <div style={{background:'#555',padding:12}}>{make('button-ghost','Ghost',{ghost:true,type:'primary'})}</div>
        <div id="button-anchor">Target</div><output id="button-count">{count}</output>
        <output id="button-request">{last}</output><output id="button-submits">{submits}</output>
    </section>;
}

function Foundation() {
    return <section data-case="foundation">
        <Space wrap>
            <Button type="primary">Primary</Button><Button>Default</Button>
            <Button type="dashed">Dashed</Button><Button type="text">Text</Button>
            <Button type="link">Link</Button><Button disabled>Disabled</Button>
            <Button danger>Danger</Button><Button loading>Loading</Button>
        </Space>
    </section>;
}

function ProgressShapes() {
    return <section data-case="progress" style={{width: 360}}>
        <Progress percent={40} success={{percent: 15}} />
        <Progress percent={60} steps={5} strokeColor={['red', 'orange', 'green']} style={{marginTop: 24}} />
        <Progress type="circle" percent={40} size={100} style={{marginTop: 24}} />
        <Progress type="dashboard" percent={70} size={100} gapDegree={90} />
        <Progress percent={100} /><Progress percent={45} status="exception" />
    </section>;
}

function ProgressGradients() {
    return <section data-case="progress-gradients" style={{width:200}}>
        <Progress percent={100} showInfo={false} strokeWidth={16} strokeLinecap="butt" strokeColor={{from:'red',to:'blue'}} />
        <Progress type="circle" percent={75} size={100} style={{marginTop:24}} strokeColor={{'0%':'red','50%':'green','100%':'blue'}} />
    </section>;
}

function TypographyShapes() {
    return <section data-case="typography" style={{width:500}}>
        {[1,2,3,4,5].map(level=><Typography.Title key={level} level={level}>Heading {level}</Typography.Title>)}
        <Typography.Paragraph>
            <Typography.Text strong>Strong</Typography.Text>{' '}<Typography.Text italic>Italic</Typography.Text>{' '}
            <Typography.Text underline>Underline</Typography.Text>{' '}<Typography.Text delete>Deleted</Typography.Text>{' '}
            <Typography.Text code>Code</Typography.Text>{' '}<Typography.Text mark>Mark</Typography.Text>{' '}
            <Typography.Text keyboard>Ctrl+C</Typography.Text>
        </Typography.Paragraph>
        <Typography.Text type="danger">Danger</Typography.Text><Typography.Link disabled href="#target">Disabled link</Typography.Link>
        <div id="target" style={{marginTop:100}}>Target</div>
    </section>;
}

function TypographyTokens() {
    const [size,setSize] = useState(16);
    return <section data-case="typography-tokens" style={{width:500}}>
        <Typography.Title id="font-default">Default</Typography.Title>
        <ConfigProvider theme={{token:{fontSize:20,colorPrimary:'#722ed1'}}}>
            <Typography.Title id="font-large">Large</Typography.Title>
            <Typography.Title id="font-large-five" level={5}>Large five</Typography.Title>
            <ConfigProvider theme={{token:{borderRadius:0}}}>
                <Typography.Title id="font-inherited">Inherited</Typography.Title>
            </ConfigProvider>
            <ConfigProvider theme={{token:{fontSize:12}}}>
                <Typography.Title id="font-small">Small</Typography.Title>
                <Typography.Title id="font-small-five" level={5}>Small five</Typography.Title>
            </ConfigProvider>
        </ConfigProvider>
        <ConfigProvider theme={{token:{fontSize:size}}}>
            <Typography.Title id="font-dynamic" level={3}>Dynamic</Typography.Title>
            <Input id="font-draft" defaultValue="seed" onChange={evt=>setSize(evt.target.value.length>4?20:16)}/>
        </ConfigProvider>
        <Typography.Title id="font-sibling" level={5}>Sibling</Typography.Title>
    </section>;
}

function TagModes() {
    const [count,setCount] = useState(0);
    const [request,setRequest] = useState('');
    const [checked,setChecked] = useState(false);
    const change = (id,value) => {setCount(n=>n+1);setRequest(`${id}:${value}`);};
    const close = (id,retain=false) => evt => {
        if(retain) evt.preventDefault();
        change(id,'close');
    };
    return <section data-case="tag-modes" style={{display:'flex',flexDirection:'column',alignItems:'flex-start',gap:12}}>
        <Tag id="tag-default">Default</Tag>
        <Tag id="tag-blue" color="blue" variant="outlined">Blue</Tag>
        <Tag id="tag-red" color="red" variant="solid">Solid</Tag>
        <Tag id="tag-success" color="success" variant="outlined" icon={<CheckCircleOutlined/>}>Success</Tag>
        <Tag id="tag-custom" color="#108ee9">Custom</Tag>
        <Tag id="tag-close" closable onClose={close('tag-close')}>Close</Tag>
        <Tag id="tag-retain" closable onClose={close('tag-retain',true)}>Retain</Tag>
        <Tag id="tag-disabled" closable disabled color="red" onClose={close('tag-disabled')}>Disabled</Tag>
        <Tag id="tag-link" href="#target">Link</Tag>
        <Tag.CheckableTag id="tag-check" checked={checked} onChange={value=>{setChecked(value);change('tag-check',value);}}>Check</Tag.CheckableTag>
        <Tag.CheckableTag id="tag-fixed" checked={false} onChange={value=>change('tag-fixed',value)}>Fixed</Tag.CheckableTag>
        <Tag.CheckableTag id="tag-check-disabled" disabled checked onChange={value=>change('tag-check-disabled',value)}>Disabled checked</Tag.CheckableTag>
        <ConfigProvider componentDisabled><Tag.CheckableTag id="tag-inherited" checked={false} onChange={value=>change('tag-inherited',value)}>Inherited</Tag.CheckableTag></ConfigProvider>
        <div id="target">Target</div><output id="tag-count">{count}</output><output id="tag-request">{request}</output><output id="tag-type"/>
    </section>;
}

function AvatarModes() {
    return <section data-case="avatar-modes" style={{display:'flex',flexDirection:'column',alignItems:'flex-start',gap:12}}>
        <Avatar id="avatar-default" aria-label="Ada">A</Avatar>
        <Avatar id="avatar-small" size="small" shape="square">A</Avatar>
        <Avatar id="avatar-large" size="large" shape="square" icon={<UserOutlined/>}/>
        <Avatar id="avatar-number" size={64}>A</Avatar>
        <Avatar id="avatar-icon" size={56} icon={<UserOutlined/>}/>
        <Avatar id="avatar-responsive" size={{xs:24,md:48,lg:64}}>A</Avatar>
        <Avatar id="avatar-custom" size={72} src={<span style={{color:'black'}}>Custom</span>}/>
    </section>;
}

function SegmentedModes() {
    const [count,setCount] = useState(0);
    const [request,setRequest] = useState('');
    const [entries,setEntries] = useState('');
    const icon = <svg width="14" height="14" viewBox="0 0 14 14"><rect x="2" y="2" width="10" height="10" fill="currentColor"/></svg>;
    const options = [{value:1,label:'One',icon},{value:2,label:'Skip',disabled:true},{value:3,label:'Three',icon}];
    const component = (id,props={}) => <Segmented id={id} options={options} defaultValue={props.value===undefined?1:undefined}
        {...props} onChange={value=>{setCount(n=>n+1);setRequest(`${id}:${value}`);}}/>;
    return <section data-case="segmented-modes" style={{display:'flex',flexDirection:'column',alignItems:'flex-start',gap:12}}>
        {component('seg-block',{block:true,style:{width:300}})}
        {component('seg-vertical',{orientation:'vertical',style:{width:180}})}
        {component('seg-small',{block:true,size:'small',style:{width:240}})}
        {component('seg-large',{block:true,size:'large',style:{width:320}})}
        {component('seg-round',{block:true,shape:'round',style:{width:240}})}
        <div dir="rtl"><ConfigProvider direction="rtl">{component('seg-rtl',{block:true,style:{width:300}})}</ConfigProvider></div>
        {component('seg-fixed',{value:1})}
        <form onSubmit={evt=>{evt.preventDefault();setEntries([...new FormData(evt.currentTarget)].map(([key,value])=>`${key}:${value}`).join(','));}}>
            {component('seg-named',{name:'choice'})}{component('seg-disabled',{name:'ignored',disabled:true})}
            <Button id="seg-submit" htmlType="submit">Submit</Button>
        </form>
        <output id="seg-count">{count}</output><output id="seg-request">{request}</output><output id="seg-entries">{entries}</output>
    </section>;
}

function PaginationModes() {
    return <section data-case="pagination-modes">
        <Pagination total={1000} defaultCurrent={3} simple showSizeChanger />
        <Pagination total={100} current={4} simple />
        <Pagination total={100} defaultCurrent={8} simple={{readOnly:true}} />
        <Pagination total={52} size="small" showTotal={(total,bounds)=>`${bounds[0]}–${bounds[1]} of ${total}`} />
        <Pagination total={0} showTotal={(_,bounds)=>`${bounds[0]}/${bounds[1]}`} />
        <Pagination total={10} hideOnSinglePage />
    </section>;
}

function GridModes() {
    const cell = () => <div style={{height:20,width:0}}/>;
    return <section data-case="grid-modes" id="grid-main" style={{width:400}}>
        <Row id="units-row" gutter={[{xs:'1rem',md:'24px'},'1em']}>
            <Col id="units-left" span={12}><div id="units-inner" style={{height:20}}/></Col><Col span={12}>{cell()}</Col>
        </Row>
        <Row id="align-row" align={{xs:'top',md:'middle',lg:'bottom'}} justify={{xs:'start',md:'center',lg:'end'}} style={{height:80,marginTop:16}}>
            <Col id="align-cell" span={6}>{cell()}</Col>
        </Row>
        <Row id="shift-row" style={{marginTop:16}}>
            <Col id="push-cell" span={8} push={8} md={{push:0}}>{cell()}</Col>
            <Col id="pull-cell" span={8} pull={8} md={{pull:0}}>{cell()}</Col>
        </Row>
        <div dir="rtl"><ConfigProvider direction="rtl"><Row id="rtl-shift-row" style={{marginTop:16}}>
            <Col id="rtl-push" span={8} push={8} md={{push:0}}>{cell()}</Col>
            <Col id="rtl-pull" span={8} pull={8} md={{pull:0}}>{cell()}</Col>
        </Row></ConfigProvider></div>
        <Row id="flex-row" wrap={false} style={{marginTop:16}}>
            <Col id="fixed-flex" flex="100px">{cell()}</Col><Col id="one-flex" flex={1}>{cell()}</Col><Col id="two-flex" flex={2}>{cell()}</Col>
        </Row>
        <Row id="responsive-flex-row" wrap={false} style={{marginTop:16}}>
            <Col id="responsive-flex-one" xs={{flex:'80px'}} md={{flex:1}}>{cell()}</Col>
            <Col id="responsive-flex-three" xs={{flex:1}} md={{flex:3}}>{cell()}</Col>
        </Row>
        <Row id="nested-row" gutter={16} style={{marginTop:16}}><Col id="nested-col" span={12}>
            <Row id="nested-inner-row" gutter={8}><Col id="nested-inner-col" span={12}>{cell()}</Col><Col span={12}>{cell()}</Col></Row>
        </Col></Row>
        <Flex id="flex-container" gap="1rem" style={{width:400,marginTop:16}}>
            <div id="flex-fixed" style={{width:80,height:20}}/><Flex id="flex-growing" flex={1}>{cell()}</Flex>
        </Flex>
    </section>;
}

function TimelineStatistic() {
    return <section data-case="timeline-statistic" style={{width: 480}}>
        <Timeline mode="alternate" pending="Waiting" items={[
            {title: '09:00', content: 'Created', color: 'green'},
            {title: '10:00', content: 'Processing', icon:<ClockCircleOutlined />}, {title: '11:00', content: 'Failed', color: 'red'},
        ]} />
        <Timeline orientation="horizontal" style={{margin: '24px 0'}}
            items={['One', 'Two', 'Three'].map(content => ({content}))} />
        <Statistic title="Balance" value="-12345678901234567890.98765" precision={2} prefix="$" suffix="USD" />
        <Statistic loading title="Loading" />
    </section>;
}

function DisplayVariants() {
    return <section data-case="display" style={{width: 360}}>
        <Space size={24}>
            <Badge count={0}>Inbox</Badge><Badge count={0} showZero /><Badge count={120} />
            <Badge status="processing" text="Working" />
        </Space>
        <Badge.Ribbon placement="start" text="New"><Card title="Ribbon card" style={{marginTop:24}}>Content</Card></Badge.Ribbon>
        <Descriptions bordered column={{xs: 1, md: 3}} style={{marginTop: 24}} items={[
            {label: 'First', children: 'One'}, {label: 'Fill', span: 'filled', children: 'Two'},
            {label: 'Wide', span: 2, children: 'Three'}, {label: 'Last', children: 'Four'},
        ]} />
        <Descriptions column={2} layout="vertical" colon={false} size="small" items={[
            {label: 'Name', children: 'Ada'}, {label: 'Role', children: 'Owner'},
        ]} />
    </section>;
}

function SkeletonShapes() {
    return <section data-case="skeleton" style={{width:360}}>
        <Skeleton avatar={{size:48}} paragraph={{rows:2,width:['100%','60%']}} />
        <Skeleton loading={false}>Ready content</Skeleton>
        <Space style={{marginTop:24}}><Skeleton.Avatar size={48}/><Skeleton.Button/><Skeleton.Input/></Space>
        <Skeleton.Image style={{marginTop:24}}/><Skeleton.Node style={{width:80,height:80}}>Chart</Skeleton.Node>
    </section>;
}

function Surfaces() {
    const [tab,setTab] = useState('a');
    return <section data-case="surfaces" style={{width:760}}>
        <Card title="Profile" extra={<a href="#">More</a>} size="small" actions={['Edit','Save']}>
            <Card.Meta title="Ada" description="Owner" avatar={<Avatar>A</Avatar>}/>
        </Card>
        <Card style={{marginTop:16}} tabList={[{key:'a',label:'A'},{key:'b',label:'B'},{key:'c',label:'C',disabled:true}]}
            activeTabKey={tab} onTabChange={setTab}>{tab==='a'?'Alpha':tab==='b'?'Beta':'Gamma'}</Card>
        <Card style={{marginTop:16}}><Card.Grid>One</Card.Grid><Card.Grid>Two</Card.Grid><Card.Grid>Three</Card.Grid></Card>
        <ConfigProvider locale={zhCN}><Empty image={Empty.PRESENTED_IMAGE_SIMPLE} style={{marginTop:24}}><Button>Create</Button></Empty></ConfigProvider>
        <Result status="404" title="404" subTitle="Page missing" extra={<Button type="primary">Home</Button>}/>
    </section>;
}

function RateModes() {
    const [count,setCount] = useState(0);
    const [request,setRequest] = useState('');
    const changed = id => value => {setCount(count=>count+1);setRequest(`${id}:${value}`);};
    const rows = [
        ['rate-default',{ 'aria-label':'Default rating'}],
        ['rate-half',{allowHalf:true,defaultValue:2.5,'aria-label':'Half rating'}],
        ['rate-fixed',{value:2,'aria-label':'Controlled rating'}],
        ['rate-disabled',{disabled:true,defaultValue:3}],
        ['rate-readonly',{disabled:true,defaultValue:3,'aria-readonly':true}],
        ['rate-no-clear',{allowClear:false,defaultValue:2}],
        ['rate-no-keyboard',{keyboard:false,defaultValue:2}],
        ['rate-small',{size:'small',defaultValue:1}],
        ['rate-large',{size:'large',defaultValue:4}],
        ['rate-custom',{count:3,character:star=>String(star.index+1),tooltips:['Poor','Fair','Good']}],
    ];
    return <section data-case="rate-modes" style={{display:'flex',flexDirection:'column',alignItems:'flex-start',gap:16}}>
        {rows.map(([id,props])=><Rate key={id} id={id} {...props} onChange={changed(id)}/>)}
        <ConfigProvider direction="rtl"><Rate id="rate-rtl" allowHalf defaultValue={2} onChange={changed('rate-rtl')}/></ConfigProvider>
        <output id="rate-count">{count}</output><output id="rate-request">{request}</output>
    </section>;
}

function AlertModes() {
    const [count,setCount] = useState(0);
    const [request,setRequest] = useState('');
    const action = (id,kind='close') => {setCount(count=>count+1);setRequest(`${id}:${kind}`);};
    return <section data-case="alert-modes" style={{width:400,display:'flex',flexDirection:'column',gap:12}}>
        <Alert id="alert-default" title="Info"/>
        <Alert id="alert-success" title="Success" type="success" showIcon/>
        <Alert id="alert-warning" title="Warning" type="warning" description="Review the details" showIcon/>
        <Alert id="alert-error" title="Error" type="error" variant="filled" closable onClose={()=>action('alert-error')}/>
        <Alert id="alert-banner" title="Banner" banner/>
        <Alert id="alert-custom" title="Custom" showIcon icon="!" action={<Button id="alert-retry" size="small" onClick={()=>action('alert-retry','click')}>Retry</Button>}/>
        <Alert id="alert-retain" title="Retain" closable={{closeIcon:'Keep','aria-label':'Keep notice'}} onClose={()=>action('alert-retain')}/>
        <Alert id="alert-parts" title="Styled" closable classNames={{title:'authored-title',close:'authored-close'}} styles={{title:{fontWeight:700},root:{borderRadius:0}}} onClose={()=>action('alert-parts')}/>
        <Alert className="alert-outer" title="Outer" closable description={<Alert className="alert-inner" title="Inner" closable onClose={()=>action(null)}/>} onClose={()=>action(null)}/>
        <ConfigProvider direction="rtl"><Alert id="alert-rtl" title="RTL" description="Right to left" showIcon closable onClose={()=>action('alert-rtl')}/></ConfigProvider>
        <output id="alert-count">{count}</output><output id="alert-request">{request}</output>
    </section>;
}

function SpinModes() {
    const [spinning,setSpinning] = useState(false);
    const [fullscreen,setFullscreen] = useState(false);
    const [removed,setRemoved] = useState(false);
    const [count,setCount] = useState(0);
    return <section data-case="spin-modes" style={{width:400,display:'flex',flexDirection:'column',alignItems:'flex-start',gap:12}}>
        <Spin id="spin-default"/>
        <Spin id="spin-small" size="small" description="Small"/>
        <Spin id="spin-large" size="large" description="Large"/>
        <Spin id="spin-idle" spinning={false}><button id="idle-content" onClick={()=>setCount(count=>count+1)}>Content</button></Spin>
        <Spin id="spin-nested" description="Loading" style={{width:400}}><div style={{height:100}}><button id="blocked-content" onClick={()=>setCount(count=>count+1)}>Content</button></div></Spin>
        <Spin id="spin-manual" percent={37}/>
        <Spin id="spin-custom" indicator={<b id="custom-indicator">Custom</b>}/>
        <Spin id="spin-parts" description="Styled" classNames={{section:'authored-section',indicator:'authored-indicator'}} styles={{description:{fontWeight:700}}}/>
        <Spin id="spin-delay" delay={5000} description="Delayed"/>
        <Spin id="spin-auto" percent="auto"/>
        <Spin id="spin-controlled" spinning={spinning} delay={300}>Controlled content</Spin>
        <Button id="toggle-spin" onClick={()=>setSpinning(value=>!value)}>Toggle</Button>
        <Button id="remove-spin" onClick={()=>setRemoved(true)}>Remove</Button>
        <Button id="toggle-fullscreen" onClick={()=>setFullscreen(value=>!value)}>Fullscreen</Button>
        <Spin id="spin-fullscreen" fullscreen spinning={fullscreen} description="Fullscreen"/>
        {!removed && <Spin id="spin-removable" delay={60000}/>}
        <output id="spin-clicks">{count}</output>
    </section>;
}

const fixture = new URLSearchParams(location.search).get('fixture') || 'foundation';
createRoot(document.getElementById('root')).render(
    <ConfigProvider theme={{token: {fontFamily: 'Reference Sans', motion: false}}}>
        {fixture === 'button-modes' ? <ButtonModes /> : fixture === 'composition' ? <Composition /> : fixture === 'progress' ? <ProgressShapes /> :
            fixture === 'progress-gradients' ? <ProgressGradients /> : fixture === 'typography' ? <TypographyShapes /> :
            fixture === 'typography-tokens' ? <TypographyTokens /> :
            fixture === 'segmented-modes' ? <SegmentedModes /> :
            fixture === 'avatar-modes' ? <AvatarModes /> :
            fixture === 'tag-modes' ? <TagModes /> :
            fixture === 'rate-modes' ? <RateModes /> :
            fixture === 'alert-modes' ? <AlertModes /> :
            fixture === 'spin-modes' ? <SpinModes /> :
            fixture === 'pagination-modes' ? <PaginationModes /> :
            fixture === 'grid-modes' ? <GridModes /> :
            fixture === 'timeline-statistic' ? <TimelineStatistic /> : fixture === 'display' ? <DisplayVariants /> :
            fixture === 'skeleton' ? <SkeletonShapes /> : fixture === 'surfaces' ? <Surfaces /> : <Foundation />}
    </ConfigProvider>
);
