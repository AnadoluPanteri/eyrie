#include <QDebug>
#include <QTimer>
#include <QDateTime>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QUrl>
#include <qjson/parser.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <Codegen.h>
#include <string.h>
#include "earie.h"


Earie::Earie(QObject *parent) : QObject(parent) {
}


void Earie::record() {
	QVariant ret;
	if(recbin != NULL) {
		qDebug() << "Ending recording";
		gst_element_set_state(recbin, GST_STATE_NULL);
		recbin = NULL;
		QMetaObject::invokeMethod(parent(), "reset", Q_RETURN_ARG(QVariant, ret));
		return;
	}
	qDebug() << "Starting recording";
	QMetaObject::invokeMethod(parent(), "setStatus", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, "Recording..."));
	recbin = gst_pipeline_new("pipeline");
	GError *err = NULL;
	recbin = gst_parse_launch("autoaudiosrc ! level ! audioconvert ! audioresample ! appsink name=asink caps=audio/x-raw-float,channels=1,rate=11025,width=32,endianness=1234", &err);
	sink = gst_bin_get_by_name(GST_BIN(recbin), "asink");
	gst_element_set_state(recbin, GST_STATE_PLAYING);
	QTimer *timer = new QTimer(this);
	timer->setSingleShot(true);
	connect(timer, SIGNAL(timeout()), this, SLOT(process()));
	timer->start(25000);
}


void Earie::process() {
	qDebug() << "Ending recording";
	if(recbin == NULL) {
		return;
	}
	QVariant ret;
	QMetaObject::invokeMethod(parent(), "setStatus", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, "Generating Echoprint Code"));
	gst_element_send_event(recbin, gst_event_new_eos());
	GstBuffer *buf = gst_buffer_new();
	GstBuffer *tmpbuf;
	while(!gst_app_sink_is_eos(GST_APP_SINK(sink))) {
		tmpbuf = gst_app_sink_pull_buffer(GST_APP_SINK(sink));
		buf = gst_buffer_join(buf, tmpbuf);
	}
	const float *pcm = (const float *) GST_BUFFER_DATA(buf);
	Codegen *codegen = new Codegen(pcm, GST_BUFFER_SIZE(buf) / sizeof(float), 0);
	std::string code = codegen->getCodeString();
	QNetworkAccessManager *networkManager = new QNetworkAccessManager();
	QUrl url("http://developer.echonest.com/api/v4/song/identify");
	QByteArray params;
	params.append("api_key=RIUKSNTIPKUMPHPEO");
	params.append("&query=[{\"metadata\":{\"version\":4.12},\"code\":\""); params.append(code.c_str()); params.append("\"}]");
	QNetworkRequest request;
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded;charset=UTF-8");
	request.setUrl(url);
	connect(networkManager, SIGNAL(finished(QNetworkReply *)), this, SLOT(parseResponse(QNetworkReply *)));
	networkManager->post(request, params);
	gst_element_set_state(recbin, GST_STATE_NULL);
	QMetaObject::invokeMethod(parent(), "setStatus", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, "Looking up song via EchoNest..."));
}


void Earie::parseResponse(QNetworkReply *reply) {
	QVariant ret;
	qDebug() << "Parsing network response";
	bool ok;
	QJson::Parser parser;
	QVariantMap result = parser.parse(reply->readAll(), &ok).toMap();
	QVariantMap response = result["response"].toMap();
	QVariantList songs = response["songs"].toList();
	if(songs.size() > 0) {
		QVariantMap song = songs[0].toMap();
		QString artist = song["artist_name"].toString();
		QString title = song["title"].toString();
		qDebug() << artist << title;
		QMetaObject::invokeMethod(parent(), "setDetails", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, artist), Q_ARG(QVariant, title));
	} else {
		QMetaObject::invokeMethod(parent(), "setStatus", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, "Sorry, we couldn't work out what\nsong that was."));
	}
	recbin = NULL;
	QMetaObject::invokeMethod(parent(), "resetButton", Q_RETURN_ARG(QVariant, ret));
}
