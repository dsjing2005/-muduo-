import { defineStore } from 'pinia'
import { ref } from 'vue'

export const Msg={
    Login:1, Register:2, RegisterAck:3, LoginAck:4, Direct:5, AddFriend:6,
    CreateGroup:7, AddGroup:8, Group:9, Logout:10, Contacts:11, Ack:12, History:13
} as const

type User={ id:number; name:string }
export type Contact=User & { state:string }
export type Group={ id:number; groupname:string; groupdesc:string; users:Contact[] }
export type Message=User & {
    msgid:typeof Msg.Direct|typeof Msg.Group; msg:string; time:string; toid?:number; groupid?:number
    messageId?:string; delivery?:'sent'|'saved'
}
export type SessionKey=`direct:${number}`|`group:${number}`
type Packet={
    msgid:number; errno?:number; errmsg?:string; requestId?:number; id?:number; name?:string
    friends?:Contact[]; groups?:Group[]; offlinemsg?:Message[]; recentDirects?:User[]
    messages?:Message[]; nextBefore?:string|null; message?:Message; deliveryPending?:boolean
}
type History={ loading:boolean; error:string; loaded:boolean; nextBefore:string|null }
type Pending={
    finish:(packet:Packet)=>void; fail:(error:Error)=>void; timer:number
}

export const useChatStore=defineStore('chat',()=>{
    const user=ref<User|null>(null)
    const contacts=ref<Contact[]>([])
    const groups=ref<Group[]>([])
    const recentDirects=ref<User[]>([])
    const history=ref<Partial<Record<SessionKey,History>>>({})
    const authenticated=ref(false)
    const messages=ref<Partial<Record<SessionKey,Message[]>>>({})
    const unread=ref<Partial<Record<SessionKey,number>>>({})
    const selected=ref<SessionKey|null>(null)
    const connection=ref<'idle'|'connecting'|'connected'|'offline'>('idle')
    const error=ref('')
    let socket:WebSocket|null=null
    let nextRequest=0
    let auth:Pending|null=null
    const pending=new Map<number,Pending>()

    function sessionKey(message:Message):SessionKey
    {
        return message.msgid===Msg.Group
            ? `group:${message.groupid!}`
            : `direct:${message.id===user.value?.id?message.toid!:message.id}`
    }

    function sortMessages(items:Message[])
    {
        items.sort((left,right)=>{
            const a=left.messageId
            const b=right.messageId
            if(a&&b) return a.length-b.length||a.localeCompare(b)
            if(a) return 1
            if(b) return -1
            return 0
        })
    }

    function addMessage(message:Message,incoming=true)
    {
        const key=sessionKey(message)
        messages.value[key]??=[]
        const items=messages.value[key]!
        const existing=message.messageId?items.find(item=>item.messageId===message.messageId):undefined
        if(existing)
        {
            if(message.delivery) existing.delivery=message.delivery
            return
        }
        items.push(message)
        sortMessages(items)
        if(incoming&&message.id!==user.value?.id&&key!==selected.value)
        {
            unread.value[key]=(unread.value[key]??0)+1
        }
    }

    function mergeHistory(key:SessionKey,page:Message[])
    {
        messages.value[key]??=[]
        const items=messages.value[key]!
        const known=new Set(items.map(message=>message.messageId))
        for(const message of page)
        {
            if(known.has(message.messageId)) continue
            items.push(message)
            known.add(message.messageId)
        }
        sortMessages(items)
    }

    async function loadHistory(key:SessionKey)
    {
        if(!authenticated.value) return
        history.value[key]??={ loading:false,error:'',loaded:false,nextBefore:null }
        const state=history.value[key]!
        if(state.loading||(state.loaded&&!state.nextBefore)) return

        const [kind,target]=key.split(':')
        const before=state.nextBefore
        state.loading=true
        state.error=''
        try
        {
            const packet=await request(Msg.History,{
                kind,targetId:Number(target),limit:30,...(before?{ before }:{})
            })
            if(history.value[key]!==state) return
            mergeHistory(key,packet.messages??[])
            state.nextBefore=packet.nextBefore??null
            state.loaded=true
        }
        catch(reason)
        {
            if(history.value[key]===state) state.error=reason instanceof Error?reason.message:'读取历史失败'
        }
        finally
        {
            state.loading=false
        }
    }

    function applyContacts(packet:Packet)
    {
        contacts.value=packet.friends??[]
        groups.value=packet.groups??[]
        recentDirects.value=packet.recentDirects??[]
    }

    function select(key:SessionKey)
    {
        selected.value=key
        unread.value[key]=0
        if(!history.value[key]?.loaded) void loadHistory(key)
    }

    function connect()
    {
        if(socket?.readyState===WebSocket.OPEN) return Promise.resolve(socket)
        if(socket?.readyState===WebSocket.CONNECTING) return Promise.reject(new Error('正在连接，请稍候'))

        connection.value='connecting'
        error.value=''
        const protocol=location.protocol==='https:'?'wss:':'ws:'
        const ws=new WebSocket(protocol+'//'+location.host+'/ws')
        socket=ws
        return new Promise<WebSocket>((resolve,reject)=>{
            const timeout=window.setTimeout(()=>{
                ws.close()
                reject(new Error('连接超时'))
            },8000)

            ws.onopen=()=>{
                clearTimeout(timeout)
                connection.value='connected'
                resolve(ws)
            }
            ws.onmessage=event=>{
                if(socket!==ws) return
                const packet=JSON.parse(event.data) as Packet
                if(packet.msgid===Msg.Direct||packet.msgid===Msg.Group)
                {
                    if(authenticated.value) addMessage(packet as unknown as Message)
                    return
                }
                const request=(packet.msgid===Msg.LoginAck||packet.msgid===Msg.RegisterAck)
                    ? auth : pending.get(packet.requestId??0)
                if(!request) return
                clearTimeout(request.timer)
                if(packet.requestId!==undefined) pending.delete(packet.requestId)
                else auth=null
                request.finish(packet)
            }
            ws.onclose=()=>{
                clearTimeout(timeout)
                if(socket!==ws) return
                socket=null
                connection.value='offline'
                authenticated.value=false
                error.value='连接已断开，请重新登录'
                const reason=new Error('连接已断开，操作结果未知')
                reject(reason)
                if(auth)
                {
                    clearTimeout(auth.timer)
                    auth.fail(reason)
                    auth=null
                }
                for(const request of pending.values())
                {
                    clearTimeout(request.timer)
                    request.fail(reason)
                }
                pending.clear()
            }
        })
    }

    async function request(msgid:number,fields:Record<string,unknown>={})
    {
        const isAuth=msgid===Msg.Login||msgid===Msg.Register
        const ws=isAuth?await connect():socket
        if(!ws||ws.readyState!==WebSocket.OPEN||(!isAuth&&!authenticated.value))
        {
            throw new Error('连接已断开，请重新登录')
        }
        if(isAuth&&auth) throw new Error('请等待上一条请求完成')
        const requestId=++nextRequest
        return new Promise<Packet>((resolve,reject)=>{
            const entry:Pending={
                finish:packet=>packet.errno?reject(new Error(packet.errmsg||'操作失败')):resolve(packet),
                fail:reject,
                timer:window.setTimeout(()=>{
                    pending.delete(requestId)
                    if(isAuth)
                    {
                        auth=null
                        ws.close()
                    }
                    reject(new Error('等待回执超时，操作结果未知'))
                },10000)
            }
            if(isAuth) auth=entry
            else pending.set(requestId,entry)
            try
            {
                ws.send(JSON.stringify(isAuth?{ msgid,...fields }:{ msgid,requestId,...fields }))
            }
            catch
            {
                clearTimeout(entry.timer)
                if(isAuth) auth=null
                else pending.delete(requestId)
                reject(new Error('发送失败，请检查连接'))
            }
        })
    }

    function clearSession()
    {
        user.value=null
        authenticated.value=false
        history.value={}
        recentDirects.value=[]
        contacts.value=[]
        groups.value=[]
        messages.value={}
        unread.value={}
        selected.value=null
    }

    async function login(id:number,password:string)
    {
        const packet=await request(Msg.Login,{ id,password })
        if(user.value?.id!==id) clearSession()
        user.value={ id:packet.id!,name:packet.name! }
        authenticated.value=true
        history.value={}
        applyContacts(packet)
        for(const message of packet.offlinemsg??[]) addMessage(message)
        let key=selected.value??(Object.keys(messages.value)[0] as SessionKey|undefined)
        if(!key&&recentDirects.value[0]) key=`direct:${recentDirects.value[0].id}`
        if(!key&&contacts.value[0]) key=`direct:${contacts.value[0].id}`
        if(!key&&groups.value[0]) key=`group:${groups.value[0].id}`
        if(key) select(key)
    }

    async function register(name:string,password:string)
    {
        return (await request(Msg.Register,{ name,password })).id!
    }

    async function changeContacts(operation:number,fields:Record<string,unknown>)
    {
        await request(operation,fields)
        try
        {
            applyContacts(await request(Msg.Contacts))
        }
        catch
        {
            throw new Error('操作已成功，列表刷新失败')
        }
    }

    async function sendMessage(key:SessionKey,text:string)
    {
        if(!authenticated.value) throw new Error('请先登录')
        const [type,rawId]=key.split(':')
        const id=Number(rawId)
        const msgid=type==='group'?Msg.Group:Msg.Direct
        const target=type==='group'?{ groupid:id }:{ toid:id }
        const packet=await request(msgid,{ ...target,msg:text })
        if(!packet.message) throw new Error('回执未包含消息，请检查会话历史')
        addMessage({ ...packet.message,delivery:packet.deliveryPending?'saved':'sent' },false)
    }

    async function logout()
    {
        if(connection.value==='connected')
        {
            try { await request(Msg.Logout) }
            finally { disconnect() }
        }
        else disconnect()
    }

    function disconnect()
    {
        const ws=socket
        const reason=new Error('连接已关闭，操作结果未知')
        if(auth)
        {
            clearTimeout(auth.timer)
            auth.fail(reason)
            auth=null
        }
        for(const request of pending.values())
        {
            clearTimeout(request.timer)
            request.fail(reason)
        }
        pending.clear()
        socket=null
        ws?.close()
        connection.value='idle'
        error.value=''
        clearSession()
    }

    return {
        user,contacts,groups,recentDirects,messages,history,unread,selected,authenticated,error,
        select,login,register,changeContacts,sendMessage,loadHistory,logout
    }
})
