import { createServer } from 'node:http'
import { createConnection } from 'node:net'
import { createReadStream } from 'node:fs'
import { stat } from 'node:fs/promises'
import { extname, resolve, sep } from 'node:path'
import { fileURLToPath } from 'node:url'
import { WebSocket, WebSocketServer } from 'ws'

const webRoot=fileURLToPath(new URL('../web/dist/',import.meta.url))
const port=Number(process.env.PORT??8080)
const chatHost=process.env.CHAT_HOST??'127.0.0.1'
const chatPort=Number(process.env.CHAT_PORT??8000)
const maxFrame=1024*1024
const contentTypes={
    '.html':'text/html; charset=utf-8',
    '.js':'text/javascript; charset=utf-8',
    '.css':'text/css; charset=utf-8'
}

const server=createServer(async (req,res)=>{
    if(req.method!=='GET'&&req.method!=='HEAD')
    {
        res.writeHead(405,{ Allow:'GET, HEAD' }).end()
        return
    }
    try
    {
        const pathname=new URL(req.url,'http://localhost').pathname
        const file=resolve(webRoot,pathname==='/'?'index.html':'.'+pathname)
        if(!file.startsWith(resolve(webRoot)+sep)) throw new Error('invalid path')
        const info=await stat(file)
        if(!info.isFile()) throw new Error('not a file')
        res.writeHead(200,{
            'Content-Type':contentTypes[extname(file)]??'application/octet-stream',
            'Content-Length':info.size,
            'X-Content-Type-Options':'nosniff'
        })
        if(req.method==='HEAD') res.end()
        else createReadStream(file).on('error',()=>res.destroy()).pipe(res)
    }
    catch
    {
        res.writeHead(404).end('Not found')
    }
})

const sockets=new WebSocketServer({ noServer:true,maxPayload:8192 })
server.on('upgrade',(req,socket,head)=>{
    try
    {
        const origin=new URL(req.headers.origin)
        if(req.url!=='/ws'||!['http:','https:'].includes(origin.protocol)||origin.host!==req.headers.host)
        {
            throw new Error('invalid origin')
        }
        sockets.handleUpgrade(req,socket,head,ws=>sockets.emit('connection',ws))
    }
    catch
    {
        socket.end('HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n')
    }
})

function decodeList(items)
{
    if(!Array.isArray(items)) return []
    return items.flatMap(item=>{
        try { return [typeof item==='string'?JSON.parse(item):item] }
        catch { return [] }
    }).filter(item=>item&&typeof item==='object'&&!Array.isArray(item))
}

sockets.on('connection',ws=>{
    const tcp=createConnection({ host:chatHost,port:chatPort })
    const connectTimeout=setTimeout(()=>tcp.destroy(),5000)
    let incoming=Buffer.alloc(0)
    let alive=true
    const heartbeat=setInterval(()=>{
        if(!alive)
        {
            ws.terminate()
            return
        }
        alive=false
        ws.ping()
    },30000)

    ws.on('pong',()=>{ alive=true })
    ws.on('message',(data,isBinary)=>{
        if(isBinary||tcp.destroyed)
        {
            ws.close(1003,'Invalid request')
            return
        }
        try
        {
            const request=JSON.parse(data.toString())
            if(!request||!Number.isInteger(request.msgid)) throw new Error('invalid request')
            if(tcp.writableLength>maxFrame)
            {
                ws.close(1013,'Upstream busy')
                return
            }
            const payload=Buffer.from(JSON.stringify(request))
            const header=Buffer.alloc(4)
            header.writeUInt32BE(payload.length)
            tcp.write(Buffer.concat([header,payload]))
        }
        catch
        {
            ws.close(1007,'Invalid JSON')
        }
    })
    ws.on('error',()=>tcp.destroy())
    ws.on('close',()=>{
        clearInterval(heartbeat)
        tcp.destroy()
    })

    tcp.on('connect',()=>clearTimeout(connectTimeout))
    tcp.on('data',chunk=>{
        incoming=Buffer.concat([incoming,chunk])
        try
        {
            while(incoming.length>=4)
            {
                const length=incoming.readUInt32BE()
                if(!length||length>maxFrame) throw new Error('invalid frame')
                if(incoming.length<length+4) return

                const event=JSON.parse(incoming.toString('utf8',4,length+4))
                incoming=incoming.subarray(length+4)
                if((event.msgid===4&&event.errno===0)||event.msgid===11)
                {
                    event.friends=decodeList(event.friends)
                    event.groups=decodeList(event.groups).map(group=>({ ...group,users:decodeList(group.users) }))
                    if(event.msgid===4) event.offlinemsg=decodeList(event.offlinemsg)
                }
                if(ws.readyState!==WebSocket.OPEN) return
                if(ws.bufferedAmount>maxFrame)
                {
                    ws.close(1013,'Client too slow')
                    return
                }
                ws.send(JSON.stringify(event))
            }
        }
        catch
        {
            ws.close(1011,'Invalid upstream message')
        }
    })
    tcp.on('error',()=>ws.close(1011,'Chat server unavailable'))
    tcp.on('close',()=>{
        clearTimeout(connectTimeout)
        ws.close(1011,'Chat server disconnected')
    })
})

server.listen(port,process.env.HOST??'127.0.0.1',()=>{
    console.log('Web gateway listening on '+JSON.stringify(server.address()))
})
