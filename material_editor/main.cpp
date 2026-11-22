#include <QApplication>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMap>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QSurfaceFormat>
#include <QVBoxLayout>
#include <QVector3D>
#include <QMatrix4x4>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

struct PreviewMaterial {
    QColor color = Qt::white;
    QString albedoMap, surfaceMap, environmentMap;
    float reflection = 0.0f, roughness = 0.5f, metalness = 0.0f;
    float coat = 0.0f, coatRough = 0.1f, coatTransmission = 1.0f;
};

static const char *vertexShader = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
uniform mat4 uModel, uMvp;
out vec3 p, n;
out vec2 uv;
void main() {
    p = vec3(uModel * vec4(aPos, 1.0));
    n = normalize(mat3(uModel) * aNormal);
    uv = aUV;
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)GLSL";

static const char *fragmentShader = R"GLSL(
#version 330 core
in vec3 p, n;
in vec2 uv;
out vec4 color;
uniform vec3 uEye, uTint;
uniform float uReflection, uRoughness, uMetalness;
uniform float uCoat, uCoatRough, uCoatTransmission;
uniform sampler2D uAlbedo, uSurface, uEnvironment;
uniform bool uHasAlbedo, uHasSurface;
const float PI = 3.141592653589793;
vec3 environment(vec3 dir, float rough) {
    dir = normalize(dir);
    vec2 uvEnv = vec2(atan(dir.z, dir.x) / (2.0 * PI) + 0.5,
                      1.0 - acos(clamp(dir.y, -1.0, 1.0)) / PI);
    return textureLod(uEnvironment, uvEnv, clamp(rough, 0.0, 1.0) * 7.0).rgb;
}
vec3 fresnel(float cosTheta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(1.0 - clamp(cosTheta, 0.0, 1.0), 5.0);
}
void main() {
    vec3 N = normalize(n), V = normalize(uEye - p);
    vec3 R = reflect(-V, N);
    float ndv = max(dot(N, V), 0.0);
    vec3 base = uTint * (uHasAlbedo ? texture(uAlbedo, uv).rgb : vec3(1.0));
    vec3 mask = uHasSurface ? texture(uSurface, uv).rgb : vec3(1.0);
    float rough = clamp(uRoughness * (uHasSurface ? mask.g : 1.0), 0.02, 1.0);
    float metal = clamp(uMetalness * (uHasSurface ? mask.b : 1.0), 0.0, 1.0);
    float strength = max(0.0, uReflection * mask.r);
    vec3 f0 = mix(vec3(0.04), base, metal);
    vec3 result = base * (1.0 - metal) * environment(N, 0.9) * 0.32;
    result += environment(R, rough) * fresnel(ndv, f0) * strength;

    // Independent studio key light, for legible highlights on the preview.
    vec3 L = normalize(vec3(-0.5, 0.85, 0.65));
    vec3 H = normalize(L + V);
    float nh = max(dot(N, H), 0.0);
    float a = max(rough * rough, 0.001);
    float d = a*a / max(PI * pow(nh*nh * (a*a - 1.0) + 1.0, 2.0), 0.0001);
    result += fresnel(max(dot(H, V), 0.0), f0) *
              d * max(dot(N, L), 0.0) * strength * 0.35;

    float coatF = 0.04 + 0.96 * pow(1.0 - ndv, 5.0);
    result = mix(result, result * (1.0 - coatF) * uCoatTransmission,
                 clamp(uCoat, 0.0, 1.0));
    result += clamp(uCoat, 0.0, 1.0) *
              environment(R, max(0.02, uCoatRough)) * coatF;
    color = vec4(max(result, vec3(0.0)), 1.0);
}
)GLSL";

class MaterialView final : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
public:
    explicit MaterialView(QWidget *parent = nullptr) : QOpenGLWidget(parent) {
        setMinimumSize(420, 340);
    }

    ~MaterialView() override {
        if (!ready_) return;
        makeCurrent();
        GLuint textures[] = {albedo_, surface_, env_};
        glDeleteTextures(3, textures);
        glDeleteBuffers(1, &vbo_);
        glDeleteVertexArrays(1, &vao_);
        doneCurrent();
    }

    void setMaterial(const PreviewMaterial &next) {
        const bool imagesChanged = next.albedoMap != material_.albedoMap ||
                                   next.surfaceMap != material_.surfaceMap ||
                                   next.environmentMap != material_.environmentMap;
        material_ = next;
        if (ready_ && imagesChanged) {
            makeCurrent();
            loadTextures();
            doneCurrent();
        }
        update();
    }
    void setPreset(int preset) {
        preset_ = preset;
        customEnv_.clear();
        if (ready_) { makeCurrent(); loadTextures(); doneCurrent(); }
        update();
    }
    void selectCustomEnvironment(const QString &path) {
        customEnv_ = path;
        if (ready_) { makeCurrent(); loadTextures(); doneCurrent(); }
        update();
    }
    void setMesh(int mesh) { plane_ = mesh != 0; update(); }

protected:
    void initializeGL() override {
        initializeOpenGLFunctions();
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_MULTISAMPLE);
        if (!shader_.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShader) ||
            !shader_.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShader) ||
            !shader_.link()) {
            qWarning("Material preview shaders could not be initialized");
        }
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, px));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, nx));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));
        glBindVertexArray(0);
        ready_ = true;
        loadTextures();
    }

    void paintGL() override {
        glClearColor(0.105f, 0.115f, 0.135f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (!shader_.isLinked()) return;
        const std::vector<Vertex> mesh = plane_ ? makePlane() : makeSphere();
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(mesh.size()*sizeof(Vertex)), mesh.data(), GL_STATIC_DRAW);
        QMatrix4x4 model, view, projection;
        model.rotate(pitch_, 1, 0, 0);
        model.rotate(yaw_, 0, 1, 0);
        const QVector3D eye(0, 0, 3.2f);
        view.lookAt(eye, QVector3D(0,0,0), QVector3D(0,1,0));
        projection.perspective(38, float(width())/float(std::max(1,height())), 0.1f, 30.0f);
        shader_.bind();
        shader_.setUniformValue("uModel", model);
        shader_.setUniformValue("uMvp", projection*view*model);
        shader_.setUniformValue("uEye", eye);
        shader_.setUniformValue("uTint", QVector3D(material_.color.redF(),
                        material_.color.greenF(), material_.color.blueF()));
        shader_.setUniformValue("uReflection", material_.reflection);
        shader_.setUniformValue("uRoughness", material_.roughness);
        shader_.setUniformValue("uMetalness", material_.metalness);
        shader_.setUniformValue("uCoat", material_.coat);
        shader_.setUniformValue("uCoatRough", material_.coatRough);
        shader_.setUniformValue("uCoatTransmission", material_.coatTransmission);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, albedo_);
        shader_.setUniformValue("uAlbedo", 0);
        shader_.setUniformValue("uHasAlbedo", albedo_ != 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, surface_);
        shader_.setUniformValue("uSurface", 1);
        shader_.setUniformValue("uHasSurface", surface_ != 0);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, env_);
        shader_.setUniformValue("uEnvironment", 2);
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, GLsizei(mesh.size()));
        glBindVertexArray(0);
        shader_.release();
    }

    void mousePressEvent(QMouseEvent *event) override { last_ = event->pos(); }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (event->buttons() & Qt::LeftButton) {
            QPoint d = event->pos() - last_;
            yaw_ += d.x() * .5f;
            pitch_ = std::clamp(pitch_ + d.y() * .5f, -85.f, 85.f);
            update();
        }
        last_ = event->pos();
    }

private:
    struct Vertex { float px,py,pz,nx,ny,nz,u,v; };
    static std::vector<Vertex> makePlane() {
        return {{-1,-1,0,0,0,1,0,0},{1,-1,0,0,0,1,1,0},{1,1,0,0,0,1,1,1},
                {-1,-1,0,0,0,1,0,0},{1,1,0,0,0,1,1,1},{-1,1,0,0,0,1,0,1}};
    }
    static std::vector<Vertex> makeSphere() {
        std::vector<Vertex> out;
        constexpr int rows = 36, cols = 48;
        constexpr float pi = 3.14159265358979323846f;
        auto vertex = [](int x, int y) -> Vertex {
            constexpr float pi = 3.14159265358979323846f;
            float u = x/48.f, v = y/36.f;
            float a = u*2*pi, b = v*pi;
            float nx = sinf(b)*cosf(a), ny = cosf(b), nz = sinf(b)*sinf(a);
            return {nx,ny,nz,nx,ny,nz,u,1-v};
        };
        out.reserve(rows*cols*6);
        for (int y=0;y<rows;y++) for (int x=0;x<cols;x++) {
            out.push_back(vertex(x,y)); out.push_back(vertex(x,y+1));
            out.push_back(vertex(x+1,y));
            out.push_back(vertex(x+1,y)); out.push_back(vertex(x,y+1));
            out.push_back(vertex(x+1,y+1));
        }
        return out;
    }

    GLuint importImage(const QString &path) {
        QImage image(path);
        if (image.isNull()) return 0;
        image = image.convertToFormat(QImage::Format_RGBA8888).mirrored(false,true);
        GLuint texture = 0;
        glGenTextures(1,&texture);
        glBindTexture(GL_TEXTURE_2D,texture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,image.width(),image.height(),
                     0,GL_RGBA,GL_UNSIGNED_BYTE,image.constBits());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
        glGenerateMipmap(GL_TEXTURE_2D);
        return texture;
    }

    GLuint presetEnvironment() {
        QImage image(512,256,QImage::Format_RGBA8888);
        for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x) {
            float u=float(x)/image.width(),v=float(y)/image.height();
            float r,g,b;
            if (preset_==1) { r=v<.5f?.38f:.19f; g=v<.5f?.56f:.22f; b=v<.5f?.82f:.18f; }
            else if (preset_==2) { r=.035f;g=.032f;b=v<.5f?.09f:.035f; }
            else { r=v<.52f?.32f:.16f;g=v<.52f?.35f:.17f;b=v<.52f?.39f:.19f; }
            if(u>.12f&&u<.25f&&v>.12f&&v<.42f) {r+=.60f;g+=.60f;b+=.60f;}
            if(u>.73f&&u<.85f&&v>.17f&&v<.38f) {r+=.45f;g+=.45f;b+=.45f;}
            image.setPixelColor(x,y,QColor::fromRgbF(std::min(1.f,r),std::min(1.f,g),std::min(1.f,b)));
        }
        // QImage scanlines run top-to-bottom; OpenGL textures run bottom-to-top.
        image = image.mirrored(false,true);
        GLuint texture=0;
        glGenTextures(1,&texture);
        glBindTexture(GL_TEXTURE_2D,texture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,image.width(),image.height(),0,
                     GL_RGBA,GL_UNSIGNED_BYTE,image.constBits());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
        glGenerateMipmap(GL_TEXTURE_2D);
        return texture;
    }

    void loadTextures() {
        const GLuint old[] = {albedo_,surface_,env_};
        glDeleteTextures(3,old);
        albedo_ = importImage(material_.albedoMap);
        surface_ = importImage(material_.surfaceMap);
        const QString envPath=customEnv_.isEmpty()?material_.environmentMap:customEnv_;
        env_ = importImage(envPath);
        if(!env_) env_=presetEnvironment();
    }

    QOpenGLShaderProgram shader_;
    PreviewMaterial material_;
    GLuint vao_=0,vbo_=0,albedo_=0,surface_=0,env_=0;
    bool ready_=false,plane_=false;
    int preset_=0;
    QString customEnv_;
    QPoint last_;
    float yaw_=-25.f,pitch_=15.f;
};

static double number(const QJsonObject &obj,const char *key,double fallback) {
    QJsonValue v=obj.value(QLatin1String(key));
    return v.isDouble()?v.toDouble():fallback;
}

class EditorWindow final : public QMainWindow {
public:
    EditorWindow() {
        resize(1160,740);
        setWindowTitle("JkGfxMod Material Editor (prototype)");
        view_=new MaterialView(this);
        QWidget *center=new QWidget(this);
        auto *layout=new QVBoxLayout(center);
        auto *toolbar=new QHBoxLayout();
        auto *meshes=new QComboBox(center);
        meshes->addItems({"Sphere","Plane"});
        auto *environments=new QComboBox(center);
        environments->addItems({"Studio","Outdoor","Night"});
        auto *custom=new QPushButton("Preview custom envmap...",center);
        toolbar->addWidget(new QLabel("Mesh:",center));toolbar->addWidget(meshes);
        toolbar->addWidget(new QLabel("Fallback envmap:",center));toolbar->addWidget(environments);
        toolbar->addWidget(custom);toolbar->addStretch();
        layout->addLayout(toolbar);layout->addWidget(view_,1);
        layout->addWidget(new QLabel("Drag to rotate. Preview is an independent approximation, not the game renderer.",center));
        setCentralWidget(center);
        connect(meshes,QOverload<int>::of(&QComboBox::currentIndexChanged),view_,&MaterialView::setMesh);
        connect(environments,QOverload<int>::of(&QComboBox::currentIndexChanged),view_,&MaterialView::setPreset);
        connect(custom,&QPushButton::clicked,this,[this] {
            QString image=QFileDialog::getOpenFileName(this,"Preview equirectangular envmap",{},
                                                       "Images (*.png *.jpg *.jpeg *.bmp)");
            if(!image.isEmpty()) view_->selectCustomEnvironment(image);
        });

        list_=new QListWidget(this);
        auto *left=new QDockWidget("Materials",this);
        left->setObjectName("MaterialsDock");
        left->setWidget(list_);addDockWidget(Qt::LeftDockWidgetArea,left);
        connect(list_,&QListWidget::currentRowChanged,this,[this](int row) {
            if(!loading_){selected_=row;refresh();}
        });

        QWidget *panel=new QWidget(this);
        auto *rightLayout=new QVBoxLayout(panel);
        auto *form=new QFormLayout();
        rightLayout->addLayout(form);
        tint_=new QPushButton("Select color...",panel);
        form->addRow("Albedo factor",tint_);
        connect(tint_,&QPushButton::clicked,this,[this] {
            if(selected_<0)return;
            auto obj=current();
            auto c=readColor(obj);
            QColor result=QColorDialog::getColor(c,this,"Albedo factor");
            if(!result.isValid())return;
            QJsonArray previous=obj.value("albedo_factor").toArray();
            double alpha=previous.size()==4?previous[3].toDouble(1.0):1.0;
            obj["albedo_factor"]=QJsonArray{result.redF(),result.greenF(),result.blueF(),alpha};
            replaceCurrent(obj);
        });
        addNumber(form,"Reflection strength","reflection_strength",0,10);
        addNumber(form,"Roughness","roughness",0,1);
        addNumber(form,"Metalness","metalness",0,1);
        addNumber(form,"Clearcoat strength","clearcoat_strength",0,1);
        addNumber(form,"Clearcoat roughness","clearcoat_roughness",0,1);
        addNumber(form,"Clearcoat transmission","clearcoat_transmission",0,1);
        textures_=new QLabel(panel);
        textures_->setWordWrap(true);
        textures_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow("Textures",textures_);
        rightLayout->addStretch();
        auto *right=new QDockWidget("Properties",this);
        right->setObjectName("PropertiesDock");
        right->setWidget(panel);addDockWidget(Qt::RightDockWidgetArea,right);

        auto *menu=menuBar()->addMenu("&File");
        auto *open=menu->addAction("&Open compiled metadata...");
        auto *save=menu->addAction("&Save metadata");
        save->setShortcut(QKeySequence::Save);
        connect(open,&QAction::triggered,this,[this]{openPack();});
        connect(save,&QAction::triggered,this,[this]{savePack();});
        statusBar()->showMessage("Open a material pack's metadata.json to begin.");
        refresh();
    }

private:
    void addNumber(QFormLayout *form,const QString &label,const QString &key,
                   double min,double max) {
        auto *box=new QDoubleSpinBox(this);
        box->setRange(min,max);box->setDecimals(3);box->setSingleStep(.01);
        values_.insert(key,box);form->addRow(label,box);
        connect(box,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,
                [this,key](double value) {
            if(loading_||selected_<0)return;
            auto obj=current();obj[key]=value;replaceCurrent(obj);
        });
    }
    QJsonObject current() const {
        QJsonArray arr=root_.value("materials").toArray();
        return selected_>=0&&selected_<arr.size()?arr[selected_].toObject():QJsonObject{};
    }
    static QColor readColor(const QJsonObject &obj) {
        auto a=obj.value("albedo_factor").toArray();
        return a.size()>=3?QColor::fromRgbF(std::clamp(a[0].toDouble(1),0.,1.),
                    std::clamp(a[1].toDouble(1),0.,1.),
                    std::clamp(a[2].toDouble(1),0.,1.)):QColor(Qt::white);
    }
    void openPack() {
        if(dirty_&&QMessageBox::question(this,"Unsaved edits","Discard unsaved changes?")!=QMessageBox::Yes)return;
        QString path=QFileDialog::getOpenFileName(this,"Open compiled material pack",file_,"JSON files (*.json)");
        if(path.isEmpty())return;
        QFile f(path);
        if(!f.open(QIODevice::ReadOnly)){QMessageBox::warning(this,"Open failed",f.errorString());return;}
        QJsonParseError error;
        auto doc=QJsonDocument::fromJson(f.readAll(),&error);
        if(error.error!=QJsonParseError::NoError||!doc.isObject()||
           !doc.object().value("materials").isArray()) {
            QMessageBox::warning(this,"Invalid pack","Expected metadata JSON with a materials array.");
            return;
        }
        file_=path;root_=doc.object();dirty_=false;loading_=true;
        list_->clear();
        auto arr=root_.value("materials").toArray();
        for(auto v:arr)list_->addItem(v.toObject().value("name").toString("(unnamed)"));
        loading_=false;
        selected_=-1;
        if(!arr.isEmpty())list_->setCurrentRow(0);else refresh();
        setWindowTitle("JkGfxMod Material Editor — "+QFileInfo(path).fileName());
        statusBar()->showMessage(QString("Loaded %1 materials").arg(arr.size()),5000);
    }
    void replaceCurrent(const QJsonObject &obj) {
        if(selected_<0)return;
        auto arr=root_.value("materials").toArray();
        arr.replace(selected_,obj);root_["materials"]=arr;dirty_=true;
        refresh();
        statusBar()->showMessage("Unsaved metadata changes",2000);
    }
    void refresh() {
        bool active=selected_>=0;
        tint_->setEnabled(active);
        for(auto *v:values_)v->setEnabled(active);
        if(!active){textures_->clear();return;}
        loading_=true;
        auto obj=current();
        const QDir folder(QFileInfo(file_).absolutePath());
        auto localPath=[&](const QJsonObject &source,const char *key) {
            QString value=source.value(QLatin1String(key)).toString();
            return value.isEmpty()?QString():folder.absoluteFilePath(value);
        };
        PreviewMaterial p;
        p.color=readColor(obj);
        p.albedoMap=localPath(obj,"albedo_map");
        p.surfaceMap=localPath(obj,"surface_map");
        p.environmentMap=localPath(obj,"environment_map");
        if(p.environmentMap.isEmpty())p.environmentMap=localPath(root_,"environment_map");
        const bool hasSurface=!p.surfaceMap.isEmpty();
        p.reflection=float(number(obj,"reflection_strength",hasSurface?1:0));
        p.roughness=float(number(obj,"roughness",hasSurface?1:0));
        p.metalness=float(number(obj,"metalness",hasSurface?1:0));
        p.coat=float(number(obj,"clearcoat_strength",obj.contains("clearcoat_surface_map")?1:0));
        p.coatRough=float(number(obj,"clearcoat_roughness",.1));
        p.coatTransmission=float(number(obj,"clearcoat_transmission",1));
        const QMap<QString,double> v={{"reflection_strength",p.reflection},
            {"roughness",p.roughness},{"metalness",p.metalness},
            {"clearcoat_strength",p.coat},{"clearcoat_roughness",p.coatRough},
            {"clearcoat_transmission",p.coatTransmission}};
        for(auto it=values_.cbegin();it!=values_.cend();++it) {
            QSignalBlocker blocker(it.value());
            it.value()->setValue(v.value(it.key()));
        }
        tint_->setStyleSheet(QString("background-color: %1").arg(p.color.name()));
        textures_->setText("Albedo: "+obj.value("albedo_map").toString("—")+
                           "\nSurface: "+obj.value("surface_map").toString("—")+
                           "\nEnvmap: "+(obj.contains("environment_map")
                              ?obj.value("environment_map").toString():
                                root_.value("environment_map").toString("Studio fallback")));
        view_->setMaterial(p);
        loading_=false;
    }
    void savePack() {
        if(file_.isEmpty()){QMessageBox::information(this,"No pack","Open compiled metadata first.");return;}
        QSaveFile file(file_);
        if(!file.open(QIODevice::WriteOnly)){QMessageBox::warning(this,"Save failed",file.errorString());return;}
        QByteArray data=QJsonDocument(root_).toJson(QJsonDocument::Indented);
        if(file.write(data)!=data.size()||!file.commit()){
            QMessageBox::warning(this,"Save failed",file.errorString());return;
        }
        dirty_=false;statusBar()->showMessage("Saved metadata.json",4000);
    }
    MaterialView *view_=nullptr;
    QListWidget *list_=nullptr;
    QPushButton *tint_=nullptr;
    QLabel *textures_=nullptr;
    QMap<QString,QDoubleSpinBox*> values_;
    QString file_;
    QJsonObject root_;
    int selected_=-1;
    bool loading_=false,dirty_=false;
};

int main(int argc,char **argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3,3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);
    QApplication app(argc,argv);
    EditorWindow editor;
    editor.show();
    return app.exec();
}
