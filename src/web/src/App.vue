<script setup lang="ts">
import { computed, nextTick, ref, watch } from 'vue'
import { Msg, useChatStore, type SessionKey } from './chat'

const chat=useChatStore()
const mode=ref<'login'|'register'>('login')
const account=ref('')
const username=ref('')
const password=ref('')
const authBusy=ref(false)
const notice=ref('')
const error=ref('')
const draft=ref('')
const sending=ref(false)
const messagesPanel=ref<HTMLElement>()
const action=ref<'friend'|'join'|'create'|null>(null)
const actionValue=ref('')
const groupDesc=ref('')
const actionBusy=ref(false)
const mobileChat=ref(false)

type Session={ key:SessionKey; name:string; kind:'direct'|'group' }
const sessions=computed(()=>{
    const list=new Map<SessionKey,Session>()
    for(const user of chat.contacts)
    {
        const key:SessionKey=`direct:${user.id}`
        list.set(key,{ key,name:user.name,kind:'direct' })
    }
    for(const group of chat.groups)
    {
        const key:SessionKey=`group:${group.id}`
        list.set(key,{ key,name:group.groupname,kind:'group' })
    }
    for(const user of chat.recentDirects)
    {
        const key:SessionKey=`direct:${user.id}`
        if(!list.has(key)) list.set(key,{ key,name:user.name,kind:'direct' })
    }
    for(const key of Object.keys(chat.messages) as SessionKey[])
    {
        if(list.has(key)) continue
        const [kind,id]=key.split(':') as [Session['kind'],string]
        const sender=chat.messages[key]?.find(message=>message.id!==chat.user?.id)
        const name=kind==='group'?'群组 '+id:(sender?.name||'用户 '+id)
        list.set(key,{ key,name,kind })
    }
    return [...list.values()].sort((left,right)=>latest(right.key).localeCompare(latest(left.key)))
})
const session=computed(()=>sessions.value.find(item=>item.key===chat.selected))
const messages=computed(()=>chat.selected?chat.messages[chat.selected]??[]:[])
const signedIn=computed(()=>chat.authenticated)
const historyState=computed(()=>chat.selected?chat.history[chat.selected]:undefined)
const draftSize=computed(()=>new TextEncoder().encode(draft.value.trim()).length)

function latest(key:SessionKey)
{
    const time=chat.messages[key]?.at(-1)?.time
    return time?normalizedTime(time):''
}

function normalizedTime(time:string)
{
    return time.replace(/^(\d{4})(\d{2})(\d{2}) (\d{2}:\d{2}:\d{2})$/,'$1-$2-$3T$4Z')
}

function showTime(time:string)
{
    const date=new Date(normalizedTime(time))
    return Number.isNaN(date.getTime())?time:new Intl.DateTimeFormat('zh-CN',{
        hour:'2-digit',minute:'2-digit',month:'numeric',day:'numeric'
    }).format(date)
}

function messageFor(reason:unknown)
{
    return reason instanceof Error?reason.message:'操作失败'
}

function positiveId(value:string)
{
    const id=Number(value)
    if(!/^\d+$/.test(value)||!Number.isInteger(id)||id<1||id>2147483647)
    {
        throw new Error('请输入有效的数字 ID')
    }
    return id
}

function openSession(key:SessionKey)
{
    if(chat.selected!==key) draft.value=''
    chat.select(key)
    mobileChat.value=true
}

function switchMode()
{
    mode.value=mode.value==='login'?'register':'login'
    error.value=''
    notice.value=''
}

async function submitAuth()
{
    const secret=password.value
    authBusy.value=true
    error.value=''
    notice.value=''
    try
    {
        if(mode.value==='register')
        {
            const id=await chat.register(username.value.trim(),secret)
            account.value=String(id)
            notice.value='注册成功。你的账号 ID 是 '+id+'，请使用该 ID 登录。'
            mode.value='login'
        }
        else
        {
            await chat.login(positiveId(account.value.trim()),secret)
            mobileChat.value=false
        }
        password.value=''
    }
    catch(reason) { error.value=messageFor(reason) }
    finally { authBusy.value=false }
}

function openAction(kind:NonNullable<typeof action.value>)
{
    action.value=kind
    actionValue.value=''
    groupDesc.value=''
    error.value=''
}

async function submitAction()
{
    actionBusy.value=true
    error.value=''
    try
    {
        if(action.value==='friend')
        {
            const id=positiveId(actionValue.value.trim())
            await chat.changeContacts(Msg.AddFriend,{ friendid:id })
            openSession(`direct:${id}`)
        }
        else if(action.value==='join')
        {
            const id=positiveId(actionValue.value.trim())
            await chat.changeContacts(Msg.AddGroup,{ groupid:id })
            openSession(`group:${id}`)
        }
        else
        {
            await chat.changeContacts(Msg.CreateGroup,{ groupname:actionValue.value.trim(),groupdesc:groupDesc.value.trim() })
        }
        action.value=null
    }
    catch(reason) { error.value=messageFor(reason) }
    finally { actionBusy.value=false }
}

async function send()
{
    if(!session.value||!draftSize.value||draftSize.value>200||sending.value) return
    const key=session.value.key
    const text=draft.value.trim()
    sending.value=true
    draft.value=''
    error.value=''
    try { await chat.sendMessage(key,text) }
    catch(reason)
    {
        if(chat.selected===key&&!draft.value) draft.value=text
        error.value=messageFor(reason)
    }
    finally { sending.value=false }
}

async function loadOlder()
{
    const key=chat.selected
    const panel=messagesPanel.value
    if(!key||!panel) return
    const distanceFromEnd=panel.scrollHeight-panel.scrollTop
    const preservePosition=chat.history[key]?.loaded
    await chat.loadHistory(key)
    await nextTick()
    if(preservePosition&&chat.selected===key&&messagesPanel.value===panel)
    {
        panel.scrollTop=panel.scrollHeight-distanceFromEnd
    }
}

function handleEnter(event:KeyboardEvent)
{
    if(event.isComposing||event.keyCode===229||event.shiftKey) return
    event.preventDefault()
    void send()
}

async function leave()
{
    error.value=''
    action.value=null
    draft.value=''
    try { await chat.logout() }
    catch(reason) { error.value=messageFor(reason) }
}

watch(()=>chat.selected,async ()=>{
    await nextTick()
    if(messagesPanel.value) messagesPanel.value.scrollTop=messagesPanel.value.scrollHeight
})

watch(()=>messages.value.at(-1),async last=>{
    const panel=messagesPanel.value
    if(!panel||!last) return
    const follow=panel.scrollHeight-panel.scrollTop-panel.clientHeight<64||last.delivery
    if(!follow) return
    await nextTick()
    if(messagesPanel.value===panel) panel.scrollTop=panel.scrollHeight
})
</script>

<template>
    <main v-if="!signedIn" class="entry">
        <section class="auth-card">
            <h1>ChatMuduo</h1>
            <h2>{{ mode==='login'?'登录':'注册' }}</h2>
            <p v-if="notice" class="notice" role="status">{{ notice }}</p>
            <p v-if="error||chat.error" class="error" role="alert">{{ error||chat.error }}</p>
            <form @submit.prevent="submitAuth">
                <label v-if="mode==='login'">账号 ID
                    <input v-model="account" inputmode="numeric" autocomplete="username" required>
                </label>
                <label v-else>用户名
                    <input v-model="username" maxlength="50" autocomplete="username" required>
                </label>
                <label>密码
                    <input v-model="password" type="password" maxlength="50"
                        :autocomplete="mode==='login'?'current-password':'new-password'" required>
                </label>
                <button class="primary" :disabled="authBusy">{{ authBusy?'请稍候…':mode==='login'?'登录':'注册' }}</button>
            </form>
            <button type="button" class="text-button" :disabled="authBusy" @click="switchMode">
                {{ mode==='login'?'创建账号':'返回登录' }}
            </button>
        </section>
    </main>

    <main v-else class="workspace" :class="{ 'mobile-chat':mobileChat }">
        <aside class="sidebar" aria-label="会话">
            <header class="identity">
                <div class="avatar">{{ chat.user?.name.charAt(0) }}</div>
                <div><strong>{{ chat.user?.name }}</strong><small>ID {{ chat.user?.id }}</small></div>
                <button type="button" class="text-button" @click="leave">退出</button>
            </header>
            <div class="sidebar-heading">
                <h1>会话</h1>
                <div class="actions">
                    <button type="button" @click="openAction('friend')">加好友</button>
                    <button type="button" @click="openAction('join')">加群</button>
                    <button type="button" @click="openAction('create')">建群</button>
                </div>
            </div>
            <form v-if="action" class="action-form" @submit.prevent="submitAction">
                <label>{{ action==='friend'?'好友 ID':action==='join'?'群组 ID':'群组名称' }}
                    <input v-model="actionValue" :inputmode="action==='create'?'text':'numeric'" maxlength="50" required>
                </label>
                <label v-if="action==='create'">群简介
                    <input v-model="groupDesc" maxlength="200">
                </label>
                <p v-if="error" class="error" role="alert">{{ error }}</p>
                <div class="actions">
                    <button type="submit" class="primary" :disabled="actionBusy">确认</button>
                    <button type="button" :disabled="actionBusy" @click="action=null; error=''">取消</button>
                </div>
            </form>
            <nav v-if="sessions.length" class="session-list" aria-label="好友和群组">
                <button v-for="item in sessions" :key="item.key" type="button"
                    :class="{ active:chat.selected===item.key }" :aria-current="chat.selected===item.key?'true':undefined"
                    @click="openSession(item.key)">
                    <span class="avatar" :class="{ group:item.kind==='group' }">{{ item.name.charAt(0) }}</span>
                    <span class="session-copy">
                        <strong>{{ item.name }}</strong>
                        <small>{{ chat.messages[item.key]?.at(-1)?.msg||(item.kind==='group'?'群聊':'私聊') }}</small>
                    </span>
                    <span v-if="chat.unread[item.key]" class="unread" :aria-label="chat.unread[item.key]+' 条未读'">{{ chat.unread[item.key] }}</span>
                </button>
            </nav>
            <p v-else class="empty">添加好友或加入群组，开始聊天。</p>
        </aside>

        <section v-if="session" class="conversation" aria-label="聊天">
            <header class="conversation-header">
                <button type="button" class="back" @click="mobileChat=false">返回</button>
                <div>
                    <h2>{{ session.name }}</h2>
                    <small>{{ session.kind==='group'?'群聊':'私聊' }} · ID {{ session.key.split(':')[1] }}</small>
                </div>
            </header>
            <div ref="messagesPanel" class="message-list" role="log" aria-label="聊天消息">
                <div v-if="historyState?.loading||historyState?.error||historyState?.nextBefore" class="history-controls">
                    <button type="button" :disabled="historyState?.loading" @click="loadOlder">
                        {{ historyState?.loading?'正在读取历史…':historyState?.error?'重试读取历史':'加载更早的消息' }}
                    </button>
                    <p v-if="historyState?.error" class="error" role="alert">{{ historyState.error }}</p>
                </div>
                <p v-if="!messages.length&&!historyState?.loading&&!historyState?.error" class="empty">这里还没有消息。</p>
                <article v-for="(message,index) in messages" :key="message.messageId??'legacy-'+index" class="message"
                    :class="{ own:message.id===chat.user?.id }">
                    <small>{{ message.name }} · {{ showTime(message.time) }}</small>
                    <p>{{ message.msg }}</p>
                    <small v-if="message.delivery==='sent'">已发送</small>
                    <small v-else-if="message.delivery==='saved'">已保存，接收方可从历史中查阅</small>
                </article>
            </div>
            <form class="composer" @submit.prevent="send">
                <p v-if="error&&!action" class="error" role="alert">{{ error }}</p>
                <textarea v-model="draft" aria-label="消息" placeholder="输入消息" rows="2"
                    @keydown.enter="handleEnter"></textarea>
                <div>
                    <small :class="{ error:draftSize>200 }">{{ draftSize }}/200 字节</small>
                    <button class="primary" :disabled="sending||!draftSize||draftSize>200">{{ sending?'发送中…':'发送' }}</button>
                </div>
            </form>
        </section>
        <section v-else class="conversation placeholder"><p>选择一个会话</p></section>
    </main>
</template>
